
#ifndef __XINDOWSUTIL_NUMBER_NUMCONV_H__
#define __XINDOWSUTIL_NUMBER_NUMCONV_H__

#define NUMCONV_STRLEN  18

XINDOWS_PUBLIC void NumberToRomanLower(LONG n, TCHAR achBuffer[NUMCONV_STRLEN]);
XINDOWS_PUBLIC void NumberToRomanUpper(LONG n, TCHAR achBuffer[NUMCONV_STRLEN]);
XINDOWS_PUBLIC void NumberToAlphaLower(LONG n, TCHAR achBuffer[NUMCONV_STRLEN]);
XINDOWS_PUBLIC void NumberToAlphaUpper(LONG n, TCHAR achBuffer[NUMCONV_STRLEN]);
XINDOWS_PUBLIC void NumberToNumeral(LONG n, TCHAR achBuffer[NUMCONV_STRLEN]);

#endif //__XINDOWSUTIL_NUMBER_NUMCONV_H__