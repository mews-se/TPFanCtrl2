// MinGW compatibility for MSVC CRT calls in upstream TPFanCtrl2.
// strlen_s is already implemented by the original tools.h; do not redefine it.
// This header is injected by _prec.h, only for GNU Windows builds.
#pragma once
#ifdef __MINGW32__
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <algorithm>
#include <tchar.h>
#include <string.h>
#ifndef __min
#define __min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef __max
#define __max(a,b) ((a)>(b)?(a):(b))
#endif
#ifndef _countof
#define _countof(a) (sizeof(a)/sizeof((a)[0]))
#endif
inline int x395_sprintf_s(char* d, size_t n, const char* fmt, ...) {
    if (!d || !n) return -1;
    va_list a; va_start(a,fmt);
    int r=vsnprintf(d,n,fmt,a); va_end(a);
    d[n-1]='\0';return r;
}
inline int x395_vsprintf_s(char* d,size_t n,const char* f,va_list a) {
    if (!d || !n) return -1;
    int r=vsnprintf(d,n,f,a);d[n-1]='\0';return r;
}
inline int x395_fopen_s(FILE** f,const char* name,const char* mode) {
    if(!f) return EINVAL;
    *f=fopen(name,mode);return *f?0:errno;
}
inline int x395_strcpy_s(char* d,size_t n,const char* s) {
    if(!d||!n||!s)return EINVAL;
    size_t k=strlen(s);if(k>=n){d[0]=0;return ERANGE;}
    memcpy(d,s,k+1);return 0;
}
inline int x395_strncpy_s(char* d,size_t n,const char* s,size_t count) {
    if(!d||!n||!s)return EINVAL;
    size_t k=std::min(std::min(strlen(s),count),n-1);
    memcpy(d,s,k);d[k]=0;return 0;
}
inline int x395_strcat_s(char* d,size_t n,const char* s) {
    if(!d||!s||!n)return EINVAL;
    size_t k=strlen(d),z=strlen(s);if(k+z>=n)return ERANGE;
    memcpy(d+k,s,z+1);return 0;
}
inline int x395_strncat_s(char* d,size_t n,const char* s,size_t count) {
    if(!d||!s||!n)return EINVAL;
    size_t k=strlen(d);if(k>=n)return ERANGE;
    size_t z=std::min(std::min(strlen(s),count),n-k-1);
    memcpy(d+k,s,z);d[k+z]=0;return 0;
}
inline int x395_itoa_s(int val,char* d,size_t n,int radix) {
    if(radix!=10 || !d || !n)return EINVAL;
    return snprintf(d,n,"%d",val)<0?ERANGE:0;
}
template<size_t N> inline int x395_itoa_s(int val,char (&d)[N],int radix) {
    return x395_itoa_s(val,d,N,radix);
}
#define sprintf_s x395_sprintf_s
#define vsprintf_s x395_vsprintf_s
#define fopen_s x395_fopen_s
#define strcpy_s x395_strcpy_s
#define strncpy_s x395_strncpy_s
#define strcat_s x395_strcat_s
#define strncat_s x395_strncat_s
#define _itoa_s x395_itoa_s
#define sscanf_s sscanf
#define _tcsncpy_s x395_strncpy_s
// strtok_s under MinGW/MSVCRT is available in some toolchains only.
// Use the C runtime strtok_s where available; old compilers can map to strtok_r.
#ifndef strtok_s
#define strtok_s strtok_r
#endif
#endif
