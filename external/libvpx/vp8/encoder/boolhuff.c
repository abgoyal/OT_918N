


#include "boolhuff.h"
#include "blockd.h"



#if defined(SECTIONBITS_OUTPUT)
unsigned __int64 Sectionbits[500];

#endif

#ifdef ENTROPY_STATS
unsigned int active_section = 0;
#endif

const unsigned int vp8_prob_cost[256] =
{
    2047, 2047, 1791, 1641, 1535, 1452, 1385, 1328, 1279, 1235, 1196, 1161, 1129, 1099, 1072, 1046,
    1023, 1000,  979,  959,  940,  922,  905,  889,  873,  858,  843,  829,  816,  803,  790,  778,
    767,  755,  744,  733,  723,  713,  703,  693,  684,  675,  666,  657,  649,  641,  633,  625,
    617,  609,  602,  594,  587,  580,  573,  567,  560,  553,  547,  541,  534,  528,  522,  516,
    511,  505,  499,  494,  488,  483,  477,  472,  467,  462,  457,  452,  447,  442,  437,  433,
    428,  424,  419,  415,  410,  406,  401,  397,  393,  389,  385,  381,  377,  373,  369,  365,
    361,  357,  353,  349,  346,  342,  338,  335,  331,  328,  324,  321,  317,  314,  311,  307,
    304,  301,  297,  294,  291,  288,  285,  281,  278,  275,  272,  269,  266,  263,  260,  257,
    255,  252,  249,  246,  243,  240,  238,  235,  232,  229,  227,  224,  221,  219,  216,  214,
    211,  208,  206,  203,  201,  198,  196,  194,  191,  189,  186,  184,  181,  179,  177,  174,
    172,  170,  168,  165,  163,  161,  159,  156,  154,  152,  150,  148,  145,  143,  141,  139,
    137,  135,  133,  131,  129,  127,  125,  123,  121,  119,  117,  115,  113,  111,  109,  107,
    105,  103,  101,   99,   97,   95,   93,   92,   90,   88,   86,   84,   82,   81,   79,   77,
    75,   73,   72,   70,   68,   66,   65,   63,   61,   60,   58,   56,   55,   53,   51,   50,
    48,   46,   45,   43,   41,   40,   38,   37,   35,   33,   32,   30,   29,   27,   25,   24,
    22,   21,   19,   18,   16,   15,   13,   12,   10,    9,    7,    6,    4,    3,    1,   1
};

void vp8_start_encode(BOOL_CODER *br, unsigned char *source)
{

    br->lowvalue = 0;
    br->range    = 255;
    br->value    = 0;
    br->count    = -24;
    br->buffer   = source;
    br->pos      = 0;
}

void vp8_stop_encode(BOOL_CODER *br)
{
    int i;

    for (i = 0; i < 32; i++)
        vp8_encode_bool(br, 0, 128);
}

DECLARE_ALIGNED(16, static const unsigned int, norm[256]) =
{
    0, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void vp8_encode_bool(BOOL_CODER *br, int bit, int probability)
{
    unsigned int split;
    int count = br->count;
    unsigned int range = br->range;
    unsigned int lowvalue = br->lowvalue;
    register unsigned int shift;

#ifdef ENTROPY_STATS
#if defined(SECTIONBITS_OUTPUT)

    if (bit)
        Sectionbits[active_section] += vp8_prob_cost[255-probability];
    else
        Sectionbits[active_section] += vp8_prob_cost[probability];

#endif
#endif

    split = 1 + (((range - 1) * probability) >> 8);

    range = split;

    if (bit)
    {
        lowvalue += split;
        range = br->range - split;
    }

    shift = norm[range];

    range <<= shift;
    count += shift;

    if (count >= 0)
    {
        int offset = shift - count;

        if ((lowvalue << (offset - 1)) & 0x80000000)
        {
            int x = br->pos - 1;

            while (x >= 0 && br->buffer[x] == 0xff)
            {
                br->buffer[x] = (unsigned char)0;
                x--;
            }

            br->buffer[x] += 1;
        }

        br->buffer[br->pos++] = (lowvalue >> (24 - offset));
        lowvalue <<= offset;
        shift = count;
        lowvalue &= 0xffffff;
        count -= 8 ;
    }

    lowvalue <<= shift;
    br->count = count;
    br->lowvalue = lowvalue;
    br->range = range;
}

void vp8_encode_value(BOOL_CODER *br, int data, int bits)
{
    int bit;

    for (bit = bits - 1; bit >= 0; bit--)
        vp8_encode_bool(br, (1 & (data >> bit)), 0x80);

}
