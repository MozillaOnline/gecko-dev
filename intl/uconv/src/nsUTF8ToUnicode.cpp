/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsUCSupport.h"
#include "nsUTF8ToUnicode.h"

#if defined(__GNUC__) && defined(__SSE2__)
// on x86_64 environment or using -msse2 such as MacOS X
#define GCC_SSE2
#endif
#if (defined(_M_IX86) || defined(_M_AMD64)) && defined(_MSC_VER) && _MSC_VER >= 1400
#define WIN_SSE2
#endif

#if defined(GCC_SSE2) || defined(WIN_SSE2)
#include "emmintrin.h"
#endif

#if defined(GCC_SSE2) || defined(_M_AMD64)
// x86_64 supports SSE2 instruction.
#define __sse2_available 1
#elif defined(WIN_SSE2)
extern "C" int __sse2_available;
#endif

#define UNICODE_BYTE_ORDER_MARK    0xFEFF

NS_IMETHODIMP NS_NewUTF8ToUnicode(nsISupports* aOuter,
                                  const nsIID& aIID,
                                  void** aResult)
{
  if (!aResult) {
    return NS_ERROR_NULL_POINTER;
  }
  if (aOuter) {
    *aResult = nsnull;
    return NS_ERROR_NO_AGGREGATION;
  }
  nsUTF8ToUnicode * inst = new nsUTF8ToUnicode();
  if (!inst) {
    *aResult = nsnull;
    return NS_ERROR_OUT_OF_MEMORY;
  }
  nsresult res = inst->QueryInterface(aIID, aResult);
  if (NS_FAILED(res)) {
    *aResult = nsnull;
    delete inst;
  }
  return res;
}

//----------------------------------------------------------------------
// Class nsUTF8ToUnicode [implementation]

nsUTF8ToUnicode::nsUTF8ToUnicode()
: nsBasicDecoderSupport()
{
  Reset();
}

//----------------------------------------------------------------------
// Subclassing of nsTableDecoderSupport class [implementation]

/**
 * Normally the maximum length of the output of the UTF8 decoder in UTF16
 *  code units is the same as the length of the input in UTF8 code units,
 *  since 1-byte, 2-byte and 3-byte UTF-8 sequences decode to a single
 *  UTF-16 character, and 4-byte UTF-8 sequences decode to a surrogate pair.
 *
 * However, there is an edge case where the output can be longer than the
 *  input: if the previous buffer ended with an incomplete multi-byte
 *  sequence and this buffer does not begin with a valid continuation
 *  byte, we will return NS_ERROR_UNEXPECTED and the caller may insert a
 *  replacement character in the output buffer which corresponds to no
 *  character in the input buffer. So in the worst case the destination
 *  will need to be one code unit longer than the source.
 *  See bug 301797.
 */
NS_IMETHODIMP nsUTF8ToUnicode::GetMaxLength(const char * aSrc,
                                            PRInt32 aSrcLength,
                                            PRInt32 * aDestLength)
{
  *aDestLength = aSrcLength + 1;
  return NS_OK;
}


//----------------------------------------------------------------------
// Subclassing of nsBasicDecoderSupport class [implementation]

NS_IMETHODIMP nsUTF8ToUnicode::Reset()
{

  mUcs4  = 0;     // cached Unicode character
  mState = 0;     // cached expected number of octets after the current octet
                  // until the beginning of the next UTF8 character sequence
  mBytes = 1;     // cached expected number of octets in the current sequence
  mFirst = PR_TRUE;

  return NS_OK;

}

//----------------------------------------------------------------------
// Subclassing of nsBasicDecoderSupport class [implementation]

// Fast ASCII -> UTF16 inner loop implementations
//
// Convert_ascii_run will update src and dst to the new values, and
// len must be the maximum number ascii chars that it would be valid
// to take from src and place into dst.  (That is, the minimum of the
// number of bytes left in src and the number of unichars available in
// dst.)

#if defined(GCC_SSE2) || defined(WIN_SSE2)

static inline void
Convert_ascii_run (const char *&src,
                   PRUnichar *&dst,
                   PRInt32 len)
{
  if (len > 15 && __sse2_available) {
    __m128i in, out1, out2;
    __m128d *outp1, *outp2;
    __m128i zeroes;
    PRUint32 offset;

    // align input to 16 bytes
    while ((NS_PTR_TO_UINT32(src) & 15) && len > 0) {
      if (*src & 0x80U)
        return;
      *dst++ = (PRUnichar) *src++;
      len--;
    }

    zeroes = _mm_setzero_si128();

    offset = NS_PTR_TO_UINT32(dst) & 15;

    // Note: all these inner loops have to break, not return; we need
    // to let the single-char loop below catch any leftover
    // byte-at-a-time ASCII chars, since this function must consume
    // all available ASCII chars before it returns

    if (offset == 0) {
      while (len > 15) {
        in = _mm_load_si128((__m128i *) src); 
        if (_mm_movemask_epi8(in))
          break;
        out1 = _mm_unpacklo_epi8(in, zeroes);
        out2 = _mm_unpackhi_epi8(in, zeroes);
        _mm_stream_si128((__m128i *) dst, out1);
        _mm_stream_si128((__m128i *) (dst + 8), out2);
        dst += 16;
        src += 16;
        len -= 16;
      }
    } else if (offset == 8) {
      outp1 = (__m128d *) &out1;
      outp2 = (__m128d *) &out2;
      while (len > 15) {
        in = _mm_load_si128((__m128i *) src); 
        if (_mm_movemask_epi8(in))
          break;
        out1 = _mm_unpacklo_epi8(in, zeroes);
        out2 = _mm_unpackhi_epi8(in, zeroes);
        _mm_storel_epi64((__m128i *) dst, out1);
        _mm_storel_epi64((__m128i *) (dst + 8), out2);
        _mm_storeh_pd((double *) (dst + 4), *outp1);
        _mm_storeh_pd((double *) (dst + 12), *outp2);
        src += 16;
        dst += 16;
        len -= 16;
      }
    } else {
      while (len > 15) {
        in = _mm_load_si128((__m128i *) src);
        if (_mm_movemask_epi8(in))
          break;
        out1 = _mm_unpacklo_epi8(in, zeroes);
        out2 = _mm_unpackhi_epi8(in, zeroes);
        _mm_storeu_si128((__m128i *) dst, out1);
        _mm_storeu_si128((__m128i *) (dst + 8), out2);
        src += 16;
        dst += 16;
        len -= 16;
      }
    }
  }

  // finish off a byte at a time

  while (len-- > 0 && (*src & 0x80U) == 0) {
    *dst++ = (PRUnichar) *src++;
  }
}

#elif defined(__arm__) || defined(_M_ARM)

// on ARM, do extra work to avoid byte/halfword reads/writes by
// reading/writing a word at a time for as long as we can
static inline void
Convert_ascii_run (const char *&src,
                   PRUnichar *&dst,
                   PRInt32 len)
{
  const PRUint32 *src32;
  PRUint32 *dst32;

  // with some alignments, we'd never actually break out of the slow loop, so
  // check and do the faster slow loop
  if ((((NS_PTR_TO_UINT32(dst) & 3) == 0) && ((NS_PTR_TO_UINT32(src) & 1) == 0)) ||
      (((NS_PTR_TO_UINT32(dst) & 3) == 2) && ((NS_PTR_TO_UINT32(src) & 1) == 1)))
  {
    while (((NS_PTR_TO_UINT32(src) & 3) ||
            (NS_PTR_TO_UINT32(dst) & 3)) &&
           len > 0)
    {
      if (*src & 0x80U)
        return;
      *dst++ = (PRUnichar) *src++;
      len--;
    }
  } else {
    goto finish;
  }

  // then go 4 bytes at a time
  src32 = (const PRUint32*) src;
  dst32 = (PRUint32*) dst;

  while (len > 4) {
    PRUint32 in = *src32++;

    if (in & 0x80808080U) {
      src32--;
      break;
    }

    *dst32++ = ((in & 0x000000ff) >>  0) | ((in & 0x0000ff00) << 8);
    *dst32++ = ((in & 0x00ff0000) >> 16) | ((in & 0xff000000) >> 8);

    len -= 4;
  }

  src = (const char *) src32;
  dst = (PRUnichar *) dst32;

finish:
  while (len-- > 0 && (*src & 0x80U) == 0) {
    *dst++ = (PRUnichar) *src++;
  }
}

#else /* generic code */

static inline void
Convert_ascii_run (const char *&src,
                   PRUnichar *&dst,
                   PRInt32 len)
{
  while (len-- > 0 && (*src & 0x80U) == 0) {
    *dst++ = (PRUnichar) *src++;
  }
}

#endif

NS_IMETHODIMP nsUTF8ToUnicode::Convert(const char * aSrc,
                                       PRInt32 * aSrcLength,
                                       PRUnichar * aDest,
                                       PRInt32 * aDestLength)
{
  PRUint32 aSrcLen   = (PRUint32) (*aSrcLength);
  PRUint32 aDestLen = (PRUint32) (*aDestLength);

  const char *in, *inend;
  inend = aSrc + aSrcLen;

  PRUnichar *out, *outend;
  outend = aDest + aDestLen;

  nsresult res = NS_OK; // conversion result

  // alias these locally for speed
  PRInt32 mUcs4 = this->mUcs4;
  PRUint8 mState = this->mState;
  PRUint8 mBytes = this->mBytes;
  PRUint8 mFirst = this->mFirst;

  // Set mFirst to PR_FALSE now so we don't have to every time through the ASCII
  // branch within the loop.
  if (mFirst && aSrcLen && (0 == (0x80 & (*aSrc))))
    mFirst = PR_FALSE;

  for (in = aSrc, out = aDest; ((in < inend) && (out < outend)); ++in) {
    if (0 == mState) {
      // When mState is zero we expect either a US-ASCII character or a
      // multi-octet sequence.
      if (0 == (0x80 & (*in))) {
        PRInt32 max_loops = PR_MIN(inend - in, outend - out);
        Convert_ascii_run(in, out, max_loops);
        --in; // match the rest of the cases
        mBytes = 1;
      } else if (0xC0 == (0xE0 & (*in))) {
        // First octet of 2 octet sequence
        mUcs4 = (PRUint32)(*in);
        mUcs4 = (mUcs4 & 0x1F) << 6;
        mState = 1;
        mBytes = 2;
      } else if (0xE0 == (0xF0 & (*in))) {
        // First octet of 3 octet sequence
        mUcs4 = (PRUint32)(*in);
        mUcs4 = (mUcs4 & 0x0F) << 12;
        mState = 2;
        mBytes = 3;
      } else if (0xF0 == (0xF8 & (*in))) {
        // First octet of 4 octet sequence
        mUcs4 = (PRUint32)(*in);
        mUcs4 = (mUcs4 & 0x07) << 18;
        mState = 3;
        mBytes = 4;
      } else if (0xF8 == (0xFC & (*in))) {
        /* First octet of 5 octet sequence.
         *
         * This is illegal because the encoded codepoint must be either
         * (a) not the shortest form or
         * (b) outside the Unicode range of 0-0x10FFFF.
         * Rather than trying to resynchronize, we will carry on until the end
         * of the sequence and let the later error handling code catch it.
         */
        mUcs4 = (PRUint32)(*in);
        mUcs4 = (mUcs4 & 0x03) << 24;
        mState = 4;
        mBytes = 5;
      } else if (0xFC == (0xFE & (*in))) {
        // First octet of 6 octet sequence, see comments for 5 octet sequence.
        mUcs4 = (PRUint32)(*in);
        mUcs4 = (mUcs4 & 1) << 30;
        mState = 5;
        mBytes = 6;
      } else {
        /* Current octet is neither in the US-ASCII range nor a legal first
         * octet of a multi-octet sequence.
         *
         * Return an error condition. Caller is responsible for flushing and
         * refilling the buffer and resetting state.
         */
        res = NS_ERROR_UNEXPECTED;
        break;
      }
    } else {
      // When mState is non-zero, we expect a continuation of the multi-octet
      // sequence
      if (0x80 == (0xC0 & (*in))) {
        // Legal continuation.
        PRUint32 shift = (mState - 1) * 6;
        PRUint32 tmp = *in;
        tmp = (tmp & 0x0000003FL) << shift;
        mUcs4 |= tmp;

        if (0 == --mState) {
          /* End of the multi-octet sequence. mUcs4 now contains the final
           * Unicode codepoint to be output
           *
           * Check for illegal sequences and codepoints.
           */

          // From Unicode 3.1, non-shortest form is illegal
          if (((2 == mBytes) && (mUcs4 < 0x0080)) ||
              ((3 == mBytes) && (mUcs4 < 0x0800)) ||
              ((4 == mBytes) && (mUcs4 < 0x10000)) ||
              (4 < mBytes) ||
              // From Unicode 3.2, surrogate characters are illegal
              ((mUcs4 & 0xFFFFF800) == 0xD800) ||
              // Codepoints outside the Unicode range are illegal
              (mUcs4 > 0x10FFFF)) {
            res = NS_ERROR_UNEXPECTED;
            break;
          }
          if (mUcs4 > 0xFFFF) {
            // mUcs4 is in the range 0x10000 - 0x10FFFF. Output a UTF-16 pair
            mUcs4 -= 0x00010000;
            *out++ = 0xD800 | (0x000003FF & (mUcs4 >> 10));
            *out++ = 0xDC00 | (0x000003FF & mUcs4);
          } else if (UNICODE_BYTE_ORDER_MARK != mUcs4 || !mFirst) {
            // Don't output the BOM only if it is the first character
            *out++ = mUcs4;
          }
          //initialize UTF8 cache
          mUcs4  = 0;
          mState = 0;
          mBytes = 1;
          mFirst = PR_FALSE;
        }
      } else {
        /* ((0xC0 & (*in) != 0x80) && (mState != 0))
         * 
         * Incomplete multi-octet sequence. Unconsume this
         * octet and return an error condition. Caller is responsible
         * for flushing and refilling the buffer and resetting state.
         */
        in--;
        res = NS_ERROR_UNEXPECTED;
        break;
      }
    }
  }

  // output not finished, output buffer too short
  if ((NS_OK == res) && (in < inend) && (out >= outend))
    res = NS_OK_UDEC_MOREOUTPUT;

  // last UCS4 is incomplete, make sure the caller
  // returns with properly aligned continuation of the buffer
  if ((NS_OK == res) && (mState != 0))
    res = NS_OK_UDEC_MOREINPUT;

  *aSrcLength = in - aSrc;
  *aDestLength = out - aDest;

  this->mUcs4 = mUcs4;
  this->mState = mState;
  this->mBytes = mBytes;
  this->mFirst = mFirst;

  return(res);
}
