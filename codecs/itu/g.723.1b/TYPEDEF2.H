/*
**
** File:        "typedef2.h"
**
*/

/*
	ITU-T G.723.1 Software Package Release 2 (June 2006)

    ITU-T G.723.1 Floating Point Speech Coder ANSI C Source Code.  Version 5.1F

    Original fixed-point code copyright (c) 1995,
    AudioCodes, DSP Group, France Telecom, Universite de Sherbrooke.
    All rights reserved.

    Floating-point code copyright (c) 1995,
    Intel Corporation and France Telecom (CNET).
    All rights reserved.
*/

/*
   Types definitions
*/
#if defined(__BORLANDC__) || defined (__WATCOMC__) || defined(_MSC_VER) || defined(__ZTC__) || defined(__HIGHC__)
typedef  long  int   Word32;
typedef  short int   Word16;
typedef  short int   Flag;
#elif defined (__sun__) || defined (__sun)
typedef short  Word16;
typedef long  Word32;
typedef int   Flag;
#elif defined(__alpha)
typedef short Word16;
typedef int   Word32;
typedef int   Flag;
#elif defined(VMS) || defined(__VMS) || defined(VAX)
typedef short  Word16;
typedef long  Word32;
typedef int   Flag;
#elif defined (__unix__) || defined (__unix)
typedef short Word16;
typedef int   Word32;
typedef int   Flag;
#else
#error  COMPILER NOT TESTED typedef2.h needs to be updated, see readme
#endif


#if defined (_single) || defined (single)
typedef  float  FLOAT;
#else
typedef  double  FLOAT;
#endif
