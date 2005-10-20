/*
  Copyright (C) 2003-2005 Brian Harring

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, US 
*/

/* as per the norm, modified version of zlib's adler32. 
   eg the code I've wroted, but the original alg/rolling chksum is 
   Mark Adler's baby....*/
   
/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995-2002 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */
#include <stdlib.h>
#include <diffball/adler32.h>
#include <diffball/defs.h>

static unsigned int PRIMES[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255,256};
//static unsigned int PRIMES[] = {1583,113,919,719,1373,659,503,373,653,1759,1289,911,1163,1109,1123,467,619,1051,109,1583,971,563,283,1619,241,1693,1543,643,181,1471,929,271,433,809,1433,257,179,1409,271,109,827,1399,1699,719,1301,1747,577,607,631,173,229,563,787,1129,463,577,1801,1559,1283,617,733,379,127,491,131,293,461,1459,239,1543,1451,821,613,1237,1493,593,1697,677,373,1427,383,1619,449,997,1571,1049,107,1031,991,1307,1181,641,107,701,863,1361,101,811,1823,1709,787,701,359,883,281,659,1523,179,719,127,1571,331,163,199,293,613,1789,317,647,421,827,1483,1289,421,241,1193,859,743,1453,199,1637,1301,1559,269,1123,433,839,1163,1021,1439,1427,157,1657,1721,1039,1453,1217,1301,1723,593,463,521,1013,1063,1069,1747,499,1753,1523,1373,1493,1213,449,1151,307,607,167,193,1193,1123,1511,449,1597,733,1553,1231,593,277,1723,113,1373,643,1721,229,613,1187,1697,281,1423,1427,1429,701,1103,353,1559,1097,241,439,1031,677,1117,619,863,499,811,1553,463,1163,1129,1093,463,857,919,1801,1493,1301,563,967,947,211,367,227,787,1637,787,643,1553,223,997,281,1021,193,1459,859,1021,127,1451,1637,347,461,1481,197,677,1051,1033,373,839,659,941,1493,101,1093,233,977,163,181};
/*static unsigned long PRIMES[] = {
  0xbcd1, 0xbb65, 0x42c2, 0xdffe, 0x9666, 0x431b, 0x8504, 0xeb46,
  0x6379, 0xd460, 0xcf14, 0x53cf, 0xdb51, 0xdb08, 0x12c8, 0xf602,
  0xe766, 0x2394, 0x250d, 0xdcbb, 0xa678, 0x02af, 0xa5c6, 0x7ea6,
  0xb645, 0xcb4d, 0xc44b, 0xe5dc, 0x9fe6, 0x5b5c, 0x35f5, 0x701a,
  0x220f, 0x6c38, 0x1a56, 0x4ca3, 0xffc6, 0xb152, 0x8d61, 0x7a58,
  0x9025, 0x8b3d, 0xbf0f, 0x95a3, 0xe5f4, 0xc127, 0x3bed, 0x320b,
  0xb7f3, 0x6054, 0x333c, 0xd383, 0x8154, 0x5242, 0x4e0d, 0x0a94,
  0x7028, 0x8689, 0x3a22, 0x0980, 0x1847, 0xb0f1, 0x9b5c, 0x4176,
  0xb858, 0xd542, 0x1f6c, 0x2497, 0x6a5a, 0x9fa9, 0x8c5a, 0x7743,
  0xa8a9, 0x9a02, 0x4918, 0x438c, 0xc388, 0x9e2b, 0x4cad, 0x01b6,
  0xab19, 0xf777, 0x365f, 0x1eb2, 0x091e, 0x7bf8, 0x7a8e, 0x5227,
  0xeab1, 0x2074, 0x4523, 0xe781, 0x01a3, 0x163d, 0x3b2e, 0x287d,
  0x5e7f, 0xa063, 0xb134, 0x8fae, 0x5e8e, 0xb7b7, 0x4548, 0x1f5a,
  0xfa56, 0x7a24, 0x900f, 0x42dc, 0xcc69, 0x02a0, 0x0b22, 0xdb31,
  0x71fe, 0x0c7d, 0x1732, 0x1159, 0xcb09, 0xe1d2, 0x1351, 0x52e9,
  0xf536, 0x5a4f, 0xc316, 0x6bf9, 0x8994, 0xb774, 0x5f3e, 0xf6d6,
  0x3a61, 0xf82c, 0xcc22, 0x9d06, 0x299c, 0x09e5, 0x1eec, 0x514f,
  0x8d53, 0xa650, 0x5c6e, 0xc577, 0x7958, 0x71ac, 0x8916, 0x9b4f,
  0x2c09, 0x5211, 0xf6d8, 0xcaaa, 0xf7ef, 0x287f, 0x7a94, 0xab49,
  0xfa2c, 0x7222, 0xe457, 0xd71a, 0x00c3, 0x1a76, 0xe98c, 0xc037,
  0x8208, 0x5c2d, 0xdfda, 0xe5f5, 0x0b45, 0x15ce, 0x8a7e, 0xfcad,
  0xaa2d, 0x4b5c, 0xd42e, 0xb251, 0x907e, 0x9a47, 0xc9a6, 0xd93f,
  0x085e, 0x35ce, 0xa153, 0x7e7b, 0x9f0b, 0x25aa, 0x5d9f, 0xc04d,
  0x8a0e, 0x2875, 0x4a1c, 0x295f, 0x1393, 0xf760, 0x9178, 0x0f5b,
  0xfa7d, 0x83b4, 0x2082, 0x721d, 0x6462, 0x0368, 0x67e2, 0x8624,
  0x194d, 0x22f6, 0x78fb, 0x6791, 0xb238, 0xb332, 0x7276, 0xf272,
  0x47ec, 0x4504, 0xa961, 0x9fc8, 0x3fdc, 0xb413, 0x007a, 0x0806,
  0x7458, 0x95c6, 0xccaa, 0x18d6, 0xe2ae, 0x1b06, 0xf3f6, 0x5050,
  0xc8e8, 0xf4ac, 0xc04c, 0xf41c, 0x992f, 0xae44, 0x5f1b, 0x1113,
  0x1738, 0xd9a8, 0x19ea, 0x2d33, 0x9698, 0x2fe9, 0x323f, 0xcde2,
  0x6d71, 0xe37d, 0xb697, 0x2c4f, 0x4373, 0x9102, 0x075d, 0x8e25,
  0x1672, 0xec28, 0x6acb, 0x86cc, 0x186e, 0x9414, 0xd674, 0xd1a5
};*/


//8={8123,8837,8219,9733,8951,8461,10193,8017,8627,9679,7933,8747,10141,7927,10141,8609,10111,9341,7937,10243,8707,8369,8681,9463,9439,9479,9391,9967,9587,9241,8221,8317,8837,8147,9551,8819,8369,9349,9029,8429,8933,8087,9719,10163,9433,9001,9161,9871,10061,9839,9283,8707,9497,8101,8293,9613,8171,8689,8819,8093,9811,8467,10211,8837,9343,9239,9283,10193,9001,9007,9749,8501,7949,8999,8627,9479,8521,9931,8369,8081,8753,8087,8081,7937,8963,9521,8693,9127,9719,9173,9769,10163,8161,9397,10103,8233,8573,9337,9323,8819,9929,9787,8009,10039,8101,9689,8747,10163,9157,9049,10091,8647,9431,9281,9643,10151,8087,9787,9857,9887,8089,10169,9871,9091,10159,10243,8761,8627,8011,8011,9497,8719,8819,8009,8647,9421,8543,9811,9851,8039,7963,8461,9601,9781,8017,9473,8117,8893,9181,9463,9349,9293,9377,10079,8867,10067,8741,8521,8839,9661,7927,9749,8737,9931,8663,8543,8219,8461,8263,9133,9403,9391,8219,8527,8669,8831,8747,10169,8963,9931,8969,8641,9767,9467,9467,10067,8929,8963,9479,9839,9029,8123,8161,9187,8287,8527,9623,9109,9743,8263,8269,8111,8741,9533,9649,8389,9151,8737,8677,8009,8629,9613,9697,9697,10141,10061,8951,9829,10243,9679,9239,8269,9041,9323,8329,8369,9397,8363,8761,9337,9283,9949,8179,9067,9811,9319,8461,10069,8719,9547,8863,9739,10169,9011,8933,8887,9241,9791,9733,10163,9629,7963,7927,9619,9539,8329};
//7={307,1129,131,683,877,503,1753,1283,1279,1621,443,347,907,839,449,701,211,1237,397,257,1669,857,479,211,1031,283,71,643,619,1091,1051,599,607,577,3,257,971,523,631,881,1361,1567,1061,1753,727,1259,397,5,1123,1217,1669,631,263,11,149,449,787,653,5,1459,487,1031,1361,1583,373,647,1087,31,1543,19,1223,223,367,5,743,1693,929,29,1499,1669,1237,1609,797,467,1181,829,59,1123,349,37,173,1741,641,1481,853,29,809,229,61,727,821,359,419,593,991,877,211,263,431,1483,1039,1213,277,439,127,419,1103,859,563,953,1439,577,67,1609,1699,1013,137,947,1259,839,1367,227,1217,179,137,587,1601,173,919,467,757,19,109,1423,661,1531,577,401,1327,1097,739,953,199,739,19,1723,967,1039,181,1103,883,1151,991,1213,757,389,1429,241,491,317,383,461,937,643,1217,811,1321,1483,311,269,73,1571,743,1361,1283,131,269,499,1637,631,1471,1579,919,103,991,383,661,1409,401,101,1091,1637,601,1321,1163,1091,1747,1453,727,1051,997,1741,1523,919,643,271,1297,1213,373,953,181,5,1489,40000,167,853,1201,829,337,1361,719,3,1483,1597,647,971,1321,947,1721,139,881,1019,1151,631,1453,991,1627,1019,1049,263,1489,233,109,1229,643,71};
//6={197,1493,1063,971,1699,563,1481,523,1613,1753,1163,211,829,577,853,109,773,271,37,1129,1733,389,1051,1753,953,1103,449,1493,827,1181,43,347,887,1433,1609,881,1559,839,769,569,1051,167,643,487,977,593,433,1069,1583,1193,443,317,71,683,1361,743,853,239,653,1607,1069,47,859,491,1549,1091,1097,107,307,1399,1249,1543,809,881,769,443,773,317,181,107,1129,1021,499,1481,1321,599,1223,487,601,653,487,163,1063,1709,7,449,1367,257,1109,1487,659,809,109,797,269,1607,523,359,809,1399,929,587,1583,1493,397,349,61,1471,1031,1549,1847,439,1307,1301,563,227,1051,647,1709,631,131,1759,41,1831,269,521,1429,193,67,751,107,683,1721,727,599,193,719,1109,181,1321,67,181,311,431,89,223,1109,1217,43,1439,409,1063,1433,1549,1103,863,571,379,1013,751,1451,337,211,29,1847,743,911,977,449,727,199,859,1637,109,433,71,421,1847,1559,1307,313,1759,1697,523,1567,521,367,1583,1301,1319,1723,331,701,1259,1291,1531,577,677,857,577,643,1039,1009,1109,1109,1669,367,787,911,271,53,1669,613,1619,1847,29,907,1429,17,1667,887,1549,733,733,139,769,659,751,821,1063,1201,1307,647,907,5,1481,1231,257,1447,409,53,109,1117,311,443,337};
//5={1451,1301,1013,3257,3023,2083,1163,1709,1487,3137,2539,2689,2423,2281,2897,1069,1931,1279,2311,1427,3347,3023,2221,1223,2017,3191,1997,1187,2521,1373,2099,1009,1733,2089,2741,1277,3257,2207,1667,2579,1301,2777,3023,3217,3319,3271,1619,2393,2137,2957,2741,2551,2003,1279,2797,2143,1259,1049,3001,1879,3433,1553,3011,2341,1697,2311,3001,3209,1231,1733,1597,1277,1997,3433,2551,1709,3463,2063,1427,1499,3119,2087,2297,3259,2153,3313,2389,1259,2423,1039,1759,1319,2621,1993,3361,2459,3389,2341,1979,1117,1579,1429,1567,3301,1163,1723,1279,2389,1259,3011,2711,1279,1487,2377,2693,2243,2027,3329,2659,3011,3089,2287,1321,2393,2683,1427,1381,2687,2467,1511,1451,3301,1619,2719,1861,1733,2099,2393,2011,2957,1777,1153,1307,3319,3271,1307,1487,1777,1129,3209,2707,3089,1747,1699,1889,1787,1993,1459,3371,3217,2467,2999,2683,1861,2663,1021,2213,2557,2143,1447,3037,3461,2027,1259,1277,1109,1279,1873,2557,1289,3191,2917,1049,1657,2521,2003,1601,2957,3413,3163,1511,1759,3389,1319,2683,2903,2251,3331,2473,1831,1481,2663,2011,2531,2437,1109,1019,3089,2693,2213,1619,3319,1667,1429,1249,2441,1619,2237,1223,1231,2417,1103,1433,1907,1447,1069,1499,1063,1087,2083,1181,1321,3433,2039,2917,2713,2473,2543,1091,3407,2897,1993,1601,1811,2917,2621,2027,1511,3433,2063,2917,1223,1621,1039,2711,2417};

//4={17681,10103,1303,13339,9461,3083,3847,8969,14009,9643,1129,13627,9173,11689,17627,19913,8849,2251,15767,18061,15767,15077,19949,3929,5281,1553,4861,13963,10837,5741,6203,9257,3217,12689,11633,11579,9949,18413,5189,4099,1289,9319,10559,6899,15511,19889,6217,1201,3371,12203,19609,4957,10337,15601,11351,19289,16001,19001,17291,19249,12539,8467,10853,5651,17597,6361,1669,19727,12377,4217,12899,8761,6449,15361,12613,15451,10957,11177,11633,13147,10559,15731,3067,4999,13807,10979,4261,5813,3067,3463,16033,2087,9467,2749,1669,5749,12227,3307,4793,6823,14551,2609,3203,19759,10337,19421,6361,11471,17377,7211,2281,7079,11497,3853,5323,9721,17471,20063,4289,12547,2767,6899,6619,2591,11867,15329,16979,5393,17393,10159,5431,9049,11057,18947,1499,2063,18701,11411,18911,5557,13049,8821,9319,6763,12911,10837,8623,17333,6863,16223,10267,6269,3547,4451,15683,9539,18149,16901,12973,9049,4643,10909,6619,4549,14827,5009,16931,19531,11981,11311,6679,8081,13003,7151,6163,5801,5347,15377,12409,14071,7937,17431,4663,11743,10337,15901,7591,14149,7907,14843,12409,17209,5441,14843,7109,6389,18127,16693,18617,18503,19681,5569,4241,9137,4373,3877,11093,1951,8563,6991,2221,10159,17807,2591,6079,14437,18917,6481,19801,14327,11257,3169,14423,17569,1087,19267,10663,18433,16361,5021,3719,19379,11393,17579,14207,2909,8933,7583,19609,11213,14923,3853,4133,14939,3299,18089,4327,17597,8467,8527,18457,6863,11159,8363,18181,11719};
//3={1583,113,919,719,1373,659,503,373,653,1759,1289,911,1163,1109,1123,467,619,1051,109,1583,971,563,283,1619,241,1693,1543,643,181,1471,929,271,433,809,1433,257,179,1409,271,109,827,1399,1699,719,1301,1747,577,607,631,173,229,563,787,1129,463,577,1801,1559,1283,617,733,379,127,491,131,293,461,1459,239,1543,1451,821,613,1237,1493,593,1697,677,373,1427,383,1619,449,997,1571,1049,107,1031,991,1307,1181,641,107,701,863,1361,101,811,1823,1709,787,701,359,883,281,659,1523,179,719,127,1571,331,163,199,293,613,1789,317,647,421,827,1483,1289,421,241,1193,859,743,1453,199,1637,1301,1559,269,1123,433,839,1163,1021,1439,1427,157,1657,1721,1039,1453,1217,1301,1723,593,463,521,1013,1063,1069,1747,499,1753,1523,1373,1493,1213,449,1151,307,607,167,193,1193,1123,1511,449,1597,733,1553,1231,593,277,1723,113,1373,643,1721,229,613,1187,1697,281,1423,1427,1429,701,1103,353,1559,1097,241,439,1031,677,1117,619,863,499,811,1553,463,1163,1129,1093,463,857,919,1801,1493,1301,563,967,947,211,367,227,787,1637,787,643,1553,223,997,281,1021,193,1459,859,1021,127,1451,1637,347,461,1481,197,677,1051,1033,373,839,659,941,1493,101,1093,233,977,163,181};
//{977,1163,1607,523,1117,269,163,1201,331,541,293,1129,1423,601,257,503,1459,7,863,419,47,1301,769,887,1429,73,563,373,673,173,241,1051,1049,37,1061,1433,197,739,499,313,409,23,19,1279,263,1481,983,17,5,1613,1231,97,463,811,79,1283,1087,1259,823,191,1181,29,211,1327,83,337,1567,61,797,593,877,937,149,227,1483,1597,709,353,449,991,743,277,137,607,1487,1151,1277,1093,761,53,401,13,1019,1033,307,1091,433,1223,229,179,859,31,1583,619,1319,1609,751,613,1367,59,1511,397,1451,1297,311,1373,947,467,131,1447,1499,1009,239,647,829,431,599,773,1039,677,251,41,1291,151,43,193,577,883,1307,881,631,443,953,1493,1021,1289,521,809,1523,1543,1303,283,1579,733,1619,929,659,1427,1063,1471,1097,971,127,1193,421,1531,617,719,1123,1559,1399,389,491,1229,1109,103,757,487,1601,557,89,1213,1217,67,787,661,1571,653,919,271,691,1013,1553,1321,911,367,101,641,157,1621,1489,587,281,727,317,839,11,1031,853,547,1381,1171,383,821,199,1549,3,109,347,1237,139,71,701,907,113,1409,1249,857,1453,479,509,571,167,643,457,233,1361,1069,1187,223,1103,569,941,379,1153,439,683,349,997,827,181,359,1439,967,461,107};

//{3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,151,157,163,167,173,179,181,191,193,197,199,211,223,227,229,233,239,241,251,257,263,269,271,277,281,283,293,307,311,313,317,331,337,347,349,353,359,367,373,379,383,389,397,401,409,419,421,431,433,439,443,449,457,461,463,467,479,487,491,499,503,509,521,523,541,547,557,563,569,571,577,587,593,599,601,607,613,617,619,631,641,643,647,653,659,661,673,677,683,691,701,709,719,727,733,739,743,751,757,761,769,773,787,797,809,811,821,823,827,829,839,853,857,859,863,877,881,883,887,907,911,919,929,937,941,947,953,967,971,977,983,991,997,1009,1013,1019,1021,1031,1033,1039,1049,1051,1061,1063,1069,1087,1091,1093,1097,1103,1109,1117,1123,1129,1151,1153,1163,1171,1181,1187,1193,1201,1213,1217,1223,1229,1231,1237,1249,1259,1277,1279,1283,1289,1291,1297,1301,1303,1307,1319,1321,1327,1361,1367,1373,1381,1399,1409,1423,1427,1429,1433,1439,1447,1451,1453,1459,1471,1481,1483,1487,1489,1493,1499,1511,1523,1531,1543,1549,1553,1559,1567,1571,1579,1583,1597,1601,1607,1609,1613,1619,1621};
//statis unsigned int POS[] 
//={5,11,17,23,31,41,47,59,67,73,83,97,103,109,127,137};

/* ========================================================================= */

int 
init_adler32_seed(ADLER32_SEED_CTX *ads, unsigned int seed_len, 
	unsigned int multi) 
{
		unsigned int x;
		ads->s1 = ads->s2 = ads->tail = 0;
		ads->seed_len = seed_len;
		ads->multi = 181;//multi;
		ads->parity=0;
		//printf("init_adler32_seed\n");
		if((ads->last_seed = (unsigned int *)
			malloc(seed_len*sizeof(int)))==NULL) {
				//printf("shite, error allocing needed memory\n");
				return MEM_ERROR;
		}
		for(x=0; x < seed_len; x++) {
				ads->last_seed[x] = 0;
		}
		if((ads->seed_chars = (unsigned char *)malloc(seed_len))==NULL) {
				return MEM_ERROR;
		}
		for(x=0; x < seed_len; x++) {
				ads->seed_chars[x] = x;
		}
		if((ads->last_parity_bits = (unsigned char *)malloc(seed_len))==NULL) {
				return MEM_ERROR;
		}
		for(x=0; x < seed_len; x++) {
				ads->last_parity_bits[x]=0;
		}
		ads->last_multi = 1;
		for(x=1; x < seed_len; x++) {
			ads->last_multi *= ads->multi;
			ads->last_multi++;
		}
	return 0;
}

void 
update_adler32_seed(ADLER32_SEED_CTX *ads, unsigned char *buff, 
	unsigned int len) 
{		
	unsigned int x;
	signed long parity;
	if(len==ads->seed_len) {
		//printf("computing seed fully\n");
		ads->s1 = ads->s2 = ads->tail =0;
		for(x=0; x < ads->seed_len; x++) {
			ads->s1 += PRIMES[buff[x]];
			ads->s2 *= ads->multi;
			ads->s2 += ads->s1;
			ads->last_seed[x] = PRIMES[buff[x]];
			ads->seed_chars[x] = buff[x];
			ads->last_parity_bits[x] = ads->last_seed[x] & 0x1;
			ads->parity += ads->last_parity_bits[x];
		}
		ads->parity &= 0x1;
		ads->tail = 0;				
	} else {
		parity = ads->parity;
		for(x=0; x < len; x++){
		ads->s1 = ads->s1 - ads->last_seed[ads->tail] + PRIMES[buff[x]];

		ads->s2 -= (ads->last_multi * ads->last_seed[ads->tail]);
		ads->s2 *= ads->multi;
		ads->s2 += ads->s1;

//		ads->s1 = ads->s1 - 
//			(ads->multi * ads->last_seed[ads->tail]) + 
//			(ads->multi * PRIMES[buff[x]]);
//		ads->s2 = ads->s2 - (ads->multi * ads->seed_len * 
//			ads->last_seed[ads->tail]) + ads->s1;
		ads->seed_chars[ads->tail] = buff[x];
		ads->last_seed[ads->tail] = PRIMES[buff[x]];
		ads->tail = (ads->tail + 1) % ads->seed_len;
//		parity -= ads->last_parity_bits[ads->tail];
//		ads->last_parity_bits[ads->tail] = 
//			ads->last_seed[ads->tail]  & 0x1;
//		parity += ads->last_parity_bits[ads->tail];
		}
//		ads->parity = (abs(parity) & 0x1);
	}
}

unsigned long 
get_checksum(ADLER32_SEED_CTX *ads)
{
//	return(((ads->s2 & 0xffff) << 16) | ((ads->s1 + ads->parity) & 0xffff));
//	return(((ads->s2 >> 2) + (ads->s1 << 3) + (ads->s2 << 16)) ^ 
//		ads->s2 ^ ads->s1);
	return ads->s2;
}

unsigned int 
free_adler32_seed(ADLER32_SEED_CTX *ads)
{
	//printf("free_adler32_seed\n");
	free(ads->last_seed);
	free(ads->seed_chars);
	free(ads->last_parity_bits);
	ads->s1 = ads->s2 = ads->parity = ads->tail = 0;
	return 0;
}

