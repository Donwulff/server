/* Copyright (C) 2000 MySQL AB & Ramil Kalimullin & MySQL Finland AB 
   & TCX DataKonsult AB
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include "myisamdef.h"

#include "rt_index.h"
#include "rt_mbr.h"

#define INTERSECT_CMP(amin, amax, bmin, bmax) ((amin >  bmax) || (bmin >  amax))
#define CONTAIN_CMP(amin, amax, bmin, bmax) ((bmin > amin)  || (bmax <  amax))
#define WITHIN_CMP(amin, amax, bmin, bmax) ((amin > bmin)  || (amax <  bmax))
#define DISJOINT_CMP(amin, amax, bmin, bmax) ((amin <= bmax) && (bmin <= amax))
#define EQUAL_CMP(amix, amax, bmin, bmax) ((amix != bmin) || (amax != bmax))

#define FCMP(A, B) ((int)(A) - (int)(B))
#define p_inc(A, B, X)  {A += X; B += X;}

#define RT_CMP(nextflag) \
  if (nextflag & MBR_INTERSECT) \
  { \
    if (INTERSECT_CMP(amin, amax, bmin, bmax)) \
      return 1; \
  } \
  else if (nextflag & MBR_CONTAIN) \
  { \
    if (CONTAIN_CMP(amin, amax, bmin, bmax)) \
      return 1; \
  } \
  else if (nextflag & MBR_WITHIN) \
  { \
    if (WITHIN_CMP(amin, amax, bmin, bmax)) \
      return 1; \
  } \
  else if (nextflag & MBR_EQUAL)  \
  { \
    if (EQUAL_CMP(amin, amax, bmin, bmax)) \
      return 1; \
  } \
  else /* if (nextflag & MBR_DISJOINT) */ \
  { \
    if (DISJOINT_CMP(amin, amax, bmin, bmax)) \
      return 1; \
  }

#define RT_CMP_KORR(type, korr_func, len, nextflag) \
{ \
  type amin, amax, bmin, bmax; \
  amin = korr_func(a); \
  bmin = korr_func(b); \
  p_inc(a, b, len); \
  amax = korr_func(a); \
  bmax = korr_func(b); \
  RT_CMP(nextflag); \
  p_inc(a, b, len); \
  break; \
}

#define RT_CMP_GET(type, get_func, len, nextflag) \
{ \
  type amin, amax, bmin, bmax; \
  get_func(amin, a); \
  get_func(bmin, b); \
  p_inc(a, b, len); \
  get_func(amax, a); \
  get_func(bmax, b); \
  RT_CMP(nextflag); \
  p_inc(a, b, len); \
  break; \
}

/*
 Compares two keys a and b depending on nextflag
 nextflag can contain these flags:
   MBR_INTERSECT(a,b)  a overlaps b
   MBR_CONTAIN(a,b)    a contains b
   MBR_DISJOINT(a,b)   a disjoint b
   MBR_WITHIN(a,b)     a within   b
   MBR_EQUAL(a,b)      All coordinates of MBRs are equal
   MBR_DATA(a,b)       Data reference is the same
 Returns 0 on success.
*/
int rtree_key_cmp(HA_KEYSEG *keyseg, uchar *b, uchar *a, uint key_length, 
                  uint nextflag)
{
  for (; (int) key_length > 0; keyseg += 2 )
  {
    key_length -= keyseg->length * 2;

    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin,amax,bmin,bmax;
        amin = (int)*((signed char *)a);
        bmin = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amax = (int)*((signed char *)a);
        bmax = (int)*((signed char *)b);
        RT_CMP(nextflag);
        p_inc(a, b, 1);
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_CMP_KORR(int16, mi_sint2korr, 2, nextflag);
    case HA_KEYTYPE_USHORT_INT:
      RT_CMP_KORR(uint16, mi_uint2korr, 2, nextflag);
    case HA_KEYTYPE_INT24:
      RT_CMP_KORR(int32, mi_sint3korr, 3, nextflag);
    case HA_KEYTYPE_UINT24:
      RT_CMP_KORR(uint32, mi_uint3korr, 3, nextflag);
    case HA_KEYTYPE_LONG_INT:
      RT_CMP_KORR(int32, mi_sint4korr, 4, nextflag);
    case HA_KEYTYPE_ULONG_INT:
      RT_CMP_KORR(uint32, mi_uint4korr, 4, nextflag);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_CMP_KORR(longlong, mi_sint8korr, 8, nextflag)
    case HA_KEYTYPE_ULONGLONG:
      RT_CMP_KORR(ulonglong, mi_uint8korr, 8, nextflag)
#endif
    case HA_KEYTYPE_FLOAT:
      RT_CMP_GET(float, mi_float4get, 4, nextflag);
    case HA_KEYTYPE_DOUBLE:
      RT_CMP_GET(double, mi_float8get, 8, nextflag);
    case HA_KEYTYPE_END:
      goto end;
    }
  }

end:
  if (nextflag & MBR_DATA)
  {
    uchar *end = a + keyseg->length;
    do
    {
      if (*a++ != *b++)
        return FCMP(a[-1], b[-1]);
    } while (a != end);
  }
  return 0;
}

#define RT_VOL_KORR(type, korr_func, len, cast) \
{ \
  type amin, amax; \
  amin = korr_func(a); \
  a += len; \
  amax = korr_func(a); \
  a += len; \
  res *= (cast(amax) - cast(amin)); \
  break; \
}

#define RT_VOL_GET(type, get_func, len, cast) \
{ \
  type amin, amax; \
  get_func(amin, a); \
  a += len; \
  get_func(amax, a); \
  a += len; \
  res *= (cast(amax) - cast(amin)); \
  break; \
}

/*
 Calculates rectangle volume
*/
double rtree_rect_volume(HA_KEYSEG *keyseg, uchar *a, uint key_length)
{
  double res = 1;
  for (; (int)key_length > 0; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax;
        amin = (int)*((signed char *)a);
        a += 1;
        amax = (int)*((signed char *)a);
        a += 1;
        res *= ((double)amax - (double)amin);
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_VOL_KORR(int16, mi_sint2korr, 2, (double));
    case HA_KEYTYPE_USHORT_INT:
      RT_VOL_KORR(uint16, mi_uint2korr, 2, (double));
    case HA_KEYTYPE_INT24:
      RT_VOL_KORR(int32, mi_sint3korr, 3, (double));
    case HA_KEYTYPE_UINT24:
      RT_VOL_KORR(uint32, mi_uint3korr, 3, (double));
    case HA_KEYTYPE_LONG_INT:
      RT_VOL_KORR(int32, mi_sint4korr, 4, (double));
    case HA_KEYTYPE_ULONG_INT:
      RT_VOL_KORR(uint32, mi_uint4korr, 4, (double));
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_VOL_KORR(longlong, mi_sint8korr, 8, (double));
    case HA_KEYTYPE_ULONGLONG:
      RT_VOL_KORR(longlong, mi_sint8korr, 8, ulonglong2double);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_VOL_GET(float, mi_float4get, 4, (double));
    case HA_KEYTYPE_DOUBLE:
      RT_VOL_GET(double, mi_float8get, 8, (double));
    case HA_KEYTYPE_END:
      key_length = 0;
      break;
    }
  }
  return res;
}

#define RT_D_MBR_KORR(type, korr_func, len, cast) \
{ \
  type amin, amax; \
  amin = korr_func(a); \
  a += len; \
  amax = korr_func(a); \
  a += len; \
  *res++ = cast(amin); \
  *res++ = cast(amax); \
  break; \
}

#define RT_D_MBR_GET(type, get_func, len, cast) \
{ \
  type amin, amax; \
  get_func(amin, a); \
  a += len; \
  get_func(amax, a); \
  a += len; \
  *res++ = cast(amin); \
  *res++ = cast(amax); \
  break; \
}

/*
 Creates an MBR as an array of doubles.
*/
int rtree_d_mbr(HA_KEYSEG *keyseg, uchar *a, uint key_length, double *res)
{
  for (; (int)key_length > 0; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax;
        amin = (int)*((signed char *)a);
        a += 1;
        amax = (int)*((signed char *)a);
        a += 1;
        *res++ = (double)amin;
        *res++ = (double)amax;
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_D_MBR_KORR(int16, mi_sint2korr, 2, (double));
    case HA_KEYTYPE_USHORT_INT:
      RT_D_MBR_KORR(uint16, mi_uint2korr, 2, (double));
    case HA_KEYTYPE_INT24:
      RT_D_MBR_KORR(int32, mi_sint3korr, 3, (double));
    case HA_KEYTYPE_UINT24:
      RT_D_MBR_KORR(uint32, mi_uint3korr, 3, (double));
    case HA_KEYTYPE_LONG_INT:
      RT_D_MBR_KORR(int32, mi_sint4korr, 4, (double));
    case HA_KEYTYPE_ULONG_INT:
      RT_D_MBR_KORR(uint32, mi_uint4korr, 4, (double));
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_D_MBR_KORR(longlong, mi_sint8korr, 8, (double));
    case HA_KEYTYPE_ULONGLONG:
      RT_D_MBR_KORR(longlong, mi_sint8korr, 8, ulonglong2double);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_D_MBR_GET(float, mi_float4get, 4, (double));
    case HA_KEYTYPE_DOUBLE:
      RT_D_MBR_GET(double, mi_float8get, 8, (double));
    case HA_KEYTYPE_END:
      key_length = 0;
      break;
    }
  }
  return 0;
}

#define RT_COMB_KORR(type, korr_func, store_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  amin = korr_func(a); \
  bmin = korr_func(b); \
  p_inc(a, b, len); \
  amax = korr_func(a); \
  bmax = korr_func(b); \
  p_inc(a, b, len); \
  amin = min(amin, bmin); \
  amax = max(amax, bmax); \
  store_func(c, amin); \
  c += len; \
  store_func(c, amax); \
  c += len; \
  break; \
}

#define RT_COMB_GET(type, get_func, store_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  get_func(amin, a); \
  get_func(bmin, b); \
  p_inc(a, b, len); \
  get_func(amax, a); \
  get_func(bmax, b); \
  p_inc(a, b, len); \
  amin = min(amin, bmin); \
  amax = max(amax, bmax); \
  store_func(c, amin); \
  c += len; \
  store_func(c, amax); \
  c += len; \
  break; \
}

/*
  Creates common minimal bounding rectungle
  for two input rectagnles a and b
  Result is written to c
*/

int rtree_combine_rect(HA_KEYSEG *keyseg, uchar* a, uchar* b, uchar* c, 
		       uint key_length)
{

  for ( ; (int) key_length > 0 ; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax, bmin, bmax;
        amin = (int)*((signed char *)a);
        bmin = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amax = (int)*((signed char *)a);
        bmax = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amin = min(amin, bmin);
        amax = max(amax, bmax);
        *((signed char*)c) = amin;
        c += 1;
        *((signed char*)c) = amax;
        c += 1;
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_COMB_KORR(int16, mi_sint2korr, mi_int2store, 2);
    case HA_KEYTYPE_USHORT_INT:
      RT_COMB_KORR(uint16, mi_uint2korr, mi_int2store, 2);
    case HA_KEYTYPE_INT24:
      RT_COMB_KORR(int32, mi_sint3korr, mi_int3store, 3);
    case HA_KEYTYPE_UINT24:
      RT_COMB_KORR(uint32, mi_uint3korr, mi_int3store, 3);
    case HA_KEYTYPE_LONG_INT:
      RT_COMB_KORR(int32, mi_sint4korr, mi_int4store, 4);
    case HA_KEYTYPE_ULONG_INT:
      RT_COMB_KORR(uint32, mi_uint4korr, mi_int4store, 4);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_COMB_KORR(longlong, mi_sint8korr, mi_int8store, 8);
    case HA_KEYTYPE_ULONGLONG:
      RT_COMB_KORR(ulonglong, mi_uint8korr, mi_int8store, 8);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_COMB_GET(float, mi_float4get, mi_float4store, 4);
    case HA_KEYTYPE_DOUBLE:
      RT_COMB_GET(double, mi_float8get, mi_float8store, 8);
    case HA_KEYTYPE_END:
      return 0;
    }
  }
  return 0;
}

#define RT_OVL_AREA_KORR(type, korr_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  amin = korr_func(a); \
  bmin = korr_func(b); \
  p_inc(a, b, len); \
  amax = korr_func(a); \
  bmax = korr_func(b); \
  p_inc(a, b, len); \
  amin = max(amin, bmin); \
  amax = min(amax, bmax); \
  if (amin >= amax) \
    return 0; \
  res *= amax - amin; \
  break; \
}

#define RT_OVL_AREA_GET(type, get_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  get_func(amin, a); \
  get_func(bmin, b); \
  p_inc(a, b, len); \
  get_func(amax, a); \
  get_func(bmax, b); \
  p_inc(a, b, len); \
  amin = max(amin, bmin); \
  amax = min(amax, bmax); \
  if (amin >= amax)  \
    return 0; \
  res *= amax - amin; \
  break; \
}

/*
Calculates overlapping area of two MBRs a & b
*/
double rtree_overlapping_area(HA_KEYSEG *keyseg, uchar* a, uchar* b, 
                             uint key_length)
{
  double res = 1;
  for (; (int) key_length > 0 ; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return -1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax, bmin, bmax;
        amin = (int)*((signed char *)a);
        bmin = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amax = (int)*((signed char *)a);
        bmax = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amin = max(amin, bmin);
        amax = min(amax, bmax);
        if (amin >= amax) 
          return 0;
        res *= amax - amin;
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_OVL_AREA_KORR(int16, mi_sint2korr, 2);
    case HA_KEYTYPE_USHORT_INT:
      RT_OVL_AREA_KORR(uint16, mi_uint2korr, 2);
    case HA_KEYTYPE_INT24:
      RT_OVL_AREA_KORR(int32, mi_sint3korr, 3);
    case HA_KEYTYPE_UINT24:
      RT_OVL_AREA_KORR(uint32, mi_uint3korr, 3);
    case HA_KEYTYPE_LONG_INT:
      RT_OVL_AREA_KORR(int32, mi_sint4korr, 4);
    case HA_KEYTYPE_ULONG_INT:
      RT_OVL_AREA_KORR(uint32, mi_uint4korr, 4);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_OVL_AREA_KORR(longlong, mi_sint8korr, 8);
    case HA_KEYTYPE_ULONGLONG:
      RT_OVL_AREA_KORR(longlong, mi_sint8korr, 8);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_OVL_AREA_GET(float, mi_float4get, 4);
    case HA_KEYTYPE_DOUBLE:
      RT_OVL_AREA_GET(double, mi_float8get, 8);
    case HA_KEYTYPE_END:
      return res;
    }
  }
  return res;
}

#define RT_AREA_INC_KORR(type, korr_func, len) \
{ \
   type amin, amax, bmin, bmax; \
   amin = korr_func(a); \
   bmin = korr_func(b); \
   p_inc(a, b, len); \
   amax = korr_func(a); \
   bmax = korr_func(b); \
   p_inc(a, b, len); \
   a_area *= (((double)amax) - ((double)amin)); \
   *ab_area *= ((double)max(amax, bmax) - (double)min(amin, bmin)); \
   break; \
}

#define RT_AREA_INC_GET(type, get_func, len)\
{\
   type amin, amax, bmin, bmax; \
   get_func(amin, a); \
   get_func(bmin, b); \
   p_inc(a, b, len); \
   get_func(amax, a); \
   get_func(bmax, b); \
   p_inc(a, b, len); \
   a_area *= (((double)amax) - ((double)amin)); \
   *ab_area *= ((double)max(amax, bmax) - (double)min(amin, bmin)); \
   break; \
}

/*
Calculates MBR_AREA(a+b) - MBR_AREA(a)
*/
double rtree_area_increase(HA_KEYSEG *keyseg, uchar* a, uchar* b, 
                          uint key_length, double *ab_area)
{
  double a_area= 1.0;
  
  *ab_area= 1.0;
  for (; (int)key_length > 0; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    /* Handle NULL part */
    if (keyseg->null_bit)
    {
      return -1;
    }

    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax, bmin, bmax;
        amin = (int)*((signed char *)a);
        bmin = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amax = (int)*((signed char *)a);
        bmax = (int)*((signed char *)b);
        p_inc(a, b, 1);
        a_area *= (((double)amax) - ((double)amin));
        *ab_area *= ((double)max(amax, bmax) - (double)min(amin, bmin));
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_AREA_INC_KORR(int16, mi_sint2korr, 2);
    case HA_KEYTYPE_USHORT_INT:
      RT_AREA_INC_KORR(uint16, mi_uint2korr, 2);
    case HA_KEYTYPE_INT24:
      RT_AREA_INC_KORR(int32, mi_sint3korr, 3);
    case HA_KEYTYPE_UINT24:
      RT_AREA_INC_KORR(int32, mi_uint3korr, 3);
    case HA_KEYTYPE_LONG_INT:
      RT_AREA_INC_KORR(int32, mi_sint4korr, 4);
    case HA_KEYTYPE_ULONG_INT:
      RT_AREA_INC_KORR(uint32, mi_uint4korr, 4);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_AREA_INC_KORR(longlong, mi_sint8korr, 8);
    case HA_KEYTYPE_ULONGLONG:
      RT_AREA_INC_KORR(longlong, mi_sint8korr, 8);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_AREA_INC_GET(float, mi_float4get, 4);
    case HA_KEYTYPE_DOUBLE:
      RT_AREA_INC_GET(double, mi_float8get, 8);
    case HA_KEYTYPE_END:
      return *ab_area - a_area;
    }
  }
  return *ab_area - a_area;
}

#define RT_PERIM_INC_KORR(type, korr_func, len) \
{ \
   type amin, amax, bmin, bmax; \
   amin = korr_func(a); \
   bmin = korr_func(b); \
   p_inc(a, b, len); \
   amax = korr_func(a); \
   bmax = korr_func(b); \
   p_inc(a, b, len); \
   a_perim+= (((double)amax) - ((double)amin)); \
   *ab_perim+= ((double)max(amax, bmax) - (double)min(amin, bmin)); \
   break; \
}

#define RT_PERIM_INC_GET(type, get_func, len)\
{\
   type amin, amax, bmin, bmax; \
   get_func(amin, a); \
   get_func(bmin, b); \
   p_inc(a, b, len); \
   get_func(amax, a); \
   get_func(bmax, b); \
   p_inc(a, b, len); \
   a_perim+= (((double)amax) - ((double)amin)); \
   *ab_perim+= ((double)max(amax, bmax) - (double)min(amin, bmin)); \
   break; \
}

/*
Calculates MBR_PERIMETER(a+b) - MBR_PERIMETER(a)
*/
double rtree_perimeter_increase(HA_KEYSEG *keyseg, uchar* a, uchar* b, 
				uint key_length, double *ab_perim)
{
  double a_perim = 0.0;
  
  *ab_perim= 0.0;
  for (; (int)key_length > 0; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    /* Handle NULL part */
    if (keyseg->null_bit)
    {
      return -1;
    }

    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax, bmin, bmax;
        amin = (int)*((signed char *)a);
        bmin = (int)*((signed char *)b);
        p_inc(a, b, 1);
        amax = (int)*((signed char *)a);
        bmax = (int)*((signed char *)b);
        p_inc(a, b, 1);
        a_perim+= (((double)amax) - ((double)amin));
        *ab_perim+= ((double)max(amax, bmax) - (double)min(amin, bmin));
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_PERIM_INC_KORR(int16, mi_sint2korr, 2);
    case HA_KEYTYPE_USHORT_INT:
      RT_PERIM_INC_KORR(uint16, mi_uint2korr, 2);
    case HA_KEYTYPE_INT24:
      RT_PERIM_INC_KORR(int32, mi_sint3korr, 3);
    case HA_KEYTYPE_UINT24:
      RT_PERIM_INC_KORR(int32, mi_uint3korr, 3);
    case HA_KEYTYPE_LONG_INT:
      RT_PERIM_INC_KORR(int32, mi_sint4korr, 4);
    case HA_KEYTYPE_ULONG_INT:
      RT_PERIM_INC_KORR(uint32, mi_uint4korr, 4);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_PERIM_INC_KORR(longlong, mi_sint8korr, 8);
    case HA_KEYTYPE_ULONGLONG:
      RT_PERIM_INC_KORR(longlong, mi_sint8korr, 8);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_PERIM_INC_GET(float, mi_float4get, 4);
    case HA_KEYTYPE_DOUBLE:
      RT_PERIM_INC_GET(double, mi_float8get, 8);
    case HA_KEYTYPE_END:
      return *ab_perim - a_perim;
    }
  }
  return *ab_perim - a_perim;
}


#define RT_PAGE_MBR_KORR(type, korr_func, store_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  amin = korr_func(k + inc); \
  amax = korr_func(k + inc + len); \
  k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag); \
  for (; k < last; k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag)) \
{ \
    bmin = korr_func(k + inc); \
    bmax = korr_func(k + inc + len); \
    if (amin > bmin) \
      amin = bmin; \
    if (amax < bmax) \
      amax = bmax; \
} \
  store_func(c, amin); \
  c += len; \
  store_func(c, amax); \
  c += len; \
  inc += 2 * len; \
  break; \
}

#define RT_PAGE_MBR_GET(type, get_func, store_func, len) \
{ \
  type amin, amax, bmin, bmax; \
  get_func(amin, k + inc); \
  get_func(amax, k + inc + len); \
  k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag); \
  for (; k < last; k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag)) \
{ \
    get_func(bmin, k + inc); \
    get_func(bmax, k + inc + len); \
    if (amin > bmin) \
      amin = bmin; \
    if (amax < bmax) \
      amax = bmax; \
} \
  store_func(c, amin); \
  c += len; \
  store_func(c, amax); \
  c += len; \
  inc += 2 * len; \
  break; \
}

/*
Calculates key page total MBR = MBR(key1) + MBR(key2) + ...
*/
int rtree_page_mbr(MI_INFO *info, HA_KEYSEG *keyseg, uchar *page_buf,
                  uchar *c, uint key_length)
{
  uint inc = 0;
  uint k_len = key_length;
  uint nod_flag = mi_test_if_nod(page_buf);
  uchar *k;
  uchar *last = rt_PAGE_END(page_buf);

  for (; (int)key_length > 0; keyseg += 2)
  {
    key_length -= keyseg->length * 2;
    
    /* Handle NULL part */
    if (keyseg->null_bit)
    {
      return 1;
    }

    k = rt_PAGE_FIRST_KEY(page_buf, nod_flag);

    switch ((enum ha_base_keytype) keyseg->type) {
    case HA_KEYTYPE_TEXT:
    case HA_KEYTYPE_BINARY:
    case HA_KEYTYPE_VARTEXT:
    case HA_KEYTYPE_VARBINARY:
    case HA_KEYTYPE_NUM:
    default:
      return 1;
      break;
    case HA_KEYTYPE_INT8:
      {
        int amin, amax, bmin, bmax;
        amin = (int)*((signed char *)(k + inc));
        amax = (int)*((signed char *)(k + inc + 1));
        k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag);
        for (; k < last; k = rt_PAGE_NEXT_KEY(k, k_len, nod_flag))
        {
          bmin = (int)*((signed char *)(k + inc));
          bmax = (int)*((signed char *)(k + inc + 1));

          if (amin > bmin)
            amin = bmin;
          if (amax < bmax)
            amax = bmax;
        }
        *((signed char*)c) = amin;
        c += 1;
        *((signed char*)c) = amax;
        c += 1;
        inc += 1 * 2;
        break;
      }
    case HA_KEYTYPE_SHORT_INT:
      RT_PAGE_MBR_KORR(int16, mi_sint2korr, mi_int2store, 2);
    case HA_KEYTYPE_USHORT_INT:
      RT_PAGE_MBR_KORR(uint16, mi_uint2korr, mi_int2store, 2);
    case HA_KEYTYPE_INT24:
      RT_PAGE_MBR_KORR(int32, mi_sint3korr, mi_int3store, 3);
    case HA_KEYTYPE_UINT24:
      RT_PAGE_MBR_KORR(uint32, mi_uint3korr, mi_int3store, 3);
    case HA_KEYTYPE_LONG_INT:
      RT_PAGE_MBR_KORR(int32, mi_sint4korr, mi_int4store, 4);
    case HA_KEYTYPE_ULONG_INT:
      RT_PAGE_MBR_KORR(uint32, mi_uint4korr, mi_int4store, 4);
#ifdef HAVE_LONG_LONG
    case HA_KEYTYPE_LONGLONG:
      RT_PAGE_MBR_KORR(longlong, mi_sint8korr, mi_int8store, 8);
    case HA_KEYTYPE_ULONGLONG:
      RT_PAGE_MBR_KORR(ulonglong, mi_uint8korr, mi_int8store, 8);
#endif
    case HA_KEYTYPE_FLOAT:
      RT_PAGE_MBR_GET(float, mi_float4get, mi_float4store, 4);
    case HA_KEYTYPE_DOUBLE:
      RT_PAGE_MBR_GET(double, mi_float8get, mi_float8store, 8);
    case HA_KEYTYPE_END:
      return 0;
    }
  }
  return 0;
}
