/*---------------------------------------------------------------------------*\

  FILE........: lsp.c
  AUTHOR......: David Rowe
  DATE CREATED: 24/2/93


  This file contains functions for LPC to LSP conversion and LSP to
  LPC conversion. Note that the LSP coefficients are not in radians
  format but in the x domain of the unit circle.

\*---------------------------------------------------------------------------*/

/*
  Copyright (C) 2009 David Rowe

  All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License version 2.1, as
  published by the Free Software Foundation.  This program is
  distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
  License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "lsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "defines.h"

/*---------------------------------------------------------------------------*\

  Introduction to Line Spectrum Pairs (LSPs)
  ------------------------------------------

  LSPs are used to encode the LPC filter coefficients {ak} for
  transmission over the channel.  LSPs have several properties (like
  less sensitivity to quantisation noise) that make them superior to
  direct quantisation of {ak}.

  A(z) is a polynomial of order lpcrdr with {ak} as the coefficients.

  A(z) is transformed to P(z) and Q(z) (using a substitution and some
  algebra), to obtain something like:

    A(z) = 0.5[P(z)(z+z^-1) + Q(z)(z-z^-1)]  (1)

  As you can imagine A(z) has complex zeros all over the z-plane. P(z)
  and Q(z) have the very neat property of only having zeros _on_ the
  unit circle.  So to find them we take a test point z=exp(jw) and
  evaluate P (exp(jw)) and Q(exp(jw)) using a grid of points between 0
  and pi.

  The zeros (roots) of P(z) also happen to alternate, which is why we
  swap coefficients as we find roots.  So the process of finding the
  LSP frequencies is basically finding the roots of 5th order
  polynomials.

  The root so P(z) and Q(z) occur in symmetrical pairs at +/-w, hence
  the name Line Spectrum Pairs (LSPs).

  To convert back to ak we just evaluate (1), "clocking" an impulse
  thru it lpcrdr times gives us the impulse response of A(z) which is
  {ak}.

\*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*\

  FUNCTION....: cheb_poly_eva()
  AUTHOR......: David Rowe
  DATE CREATED: 24/2/93

  This function evaluates a series of chebyshev polynomials

  FIXME: performing memory allocation at run time is very inefficient,
  replace with stack variables of MAX_P size.

\*---------------------------------------------------------------------------*/

static float cheb_poly_eva(float *coef, float x, int order)
/*  float coef[]  	coefficients of the polynomial to be evaluated 	*/
/*  float x   		the point where polynomial is to be evaluated 	*/
/*  int order 		order of the polynomial 			*/
{
  int i;
  float *t, *u, *v, sum;
  float *T = (float *)malloc(((order / 2) + 1) * sizeof(float));

  if (T == NULL) {
    // Handle memory allocation failure
    return 0.0f;  // Or another appropriate error value
  }

  /* Initialise pointers */
  t = T; /* T[i-2] 			*/
  *t++ = 1.0f;
  u = t--; /* T[i-1] 			*/
  *u++ = x;
  v = u--; /* T[i] 			*/

  /* Evaluate chebyshev series formulation using iterative approach 	*/
  for (i = 2; i <= order / 2; i++)
    *v++ = (2 * x) * (*u++) - *t++; /* T[i] = 2*x*T[i-1] - T[i-2]	*/

  sum = 0.0f; /* initialise sum to zero 	*/
  t = T;      /* reset pointer 		*/

  /* Evaluate polynomial and return value */
  for (i = 0; i <= order / 2; i++) sum += coef[(order / 2) - i] * (*t++);

  free(T);  // Free allocated memory
  return sum;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: lpc_to_lsp()
  AUTHOR......: David Rowe
  DATE CREATED: 24/2/93

  This function converts LPC coefficients to LSP coefficients.

\*---------------------------------------------------------------------------*/

int lpc_to_lsp(float *a, int order, float *freq, int nb, float delta)
/*  float *a 		     	lpc coefficients			*/
/*  int order			order of LPC coefficients (10) 		*/
/*  float *freq 	      	LSP frequencies in radians      	*/
/*  int nb			number of sub-intervals (4) 		*/
/*  float delta			grid spacing interval (0.02) 		*/
{
  float psuml, psumr, psumm, temp_xr, xl, xr, xm = 0;
  float temp_psumr;
  int i, j, m, flag, k;
  float *px; /* ptrs of respective P'(z) & Q'(z)	*/
  float *qx;
  float *p;
  float *q;
  float *pt;     /* ptr used for cheb_poly_eval()
                    whether P' or Q' 			*/
  int roots = 0; /* number of roots found 	        */
  float *Q = (float *)malloc((order + 1) * sizeof(float));
  float *P = (float *)malloc((order + 1) * sizeof(float));

  if (Q == NULL || P == NULL) {
    // Handle memory allocation failure
    free(Q);
    free(P);
    return -1;  // Or another appropriate error value
  }

  flag = 1;
  m = order / 2; /* order of P'(z) & Q'(z) polynimials 	*/

  /* determine P'(z)'s and Q'(z)'s coefficients where
    P'(z) = P(z)/(1 + z^(-1)) and Q'(z) = Q(z)/(1-z^(-1)) */
  px = P; /* initilaise ptrs */
  qx = Q;
  p = px;
  q = qx;
  *px++ = 1.0f;
  *qx++ = 1.0f;
  for (i = 1; i <= m; i++) {
    *px++ = a[i] + a[order + 1 - i] - *p++;
    *qx++ = a[i] - a[order + 1 - i] + *q++;
  }
  px = P;
  qx = Q;
  for (i = 0; i < m; i++) {
    *px = 2 * *px;
    *qx = 2 * *qx;
    px++;
    qx++;
  }
  px = P; /* re-initialise ptrs 			*/
  qx = Q;

  /* Search for a zero in P'(z) polynomial first and then alternate to Q'(z).
  Keep alternating between the two polynomials as each zero is found 	*/
  xr = 0;    /* initialise xr to zero 		*/
  xl = 1.0f; /* start at point xl = 1 		*/

  for (j = 0; j < order; j++) {
    if (j % 2) /* determines whether P' or Q' is eval. */
      pt = qx;
    else
      pt = px;
    psuml = cheb_poly_eva(pt, xl, order); /* evals poly. at xl 	*/
    flag = 1;
    while (flag && (xr >= -1.0f)) {
      xr = xl - delta;                      /* interval spacing 	*/
      psumr = cheb_poly_eva(pt, xr, order); /* poly(xl-delta_x) 	*/
      temp_psumr = psumr;
      temp_xr = xr;

      if (((psumr * psuml) < 0.0f) || (psumr == 0.0f)) {
        roots++;
        psumm = psuml;
        for (k = 0; k <= nb; k++) {
          xm = (xl + xr) / 2; /* bisect the interval 	*/
          psumm = cheb_poly_eva(pt, xm, order);
          if (psumm * psuml > 0.0f) {
            psuml = psumm;
            xl = xm;
          } else {
            psumr = psumm;
            xr = xm;
          }
        }
        /* once zero is found, reset initial interval to xr 	*/
        freq[j] = xm;
        xl = xm;
        flag = 0; /* reset flag for next search 	*/
      } else {
        psuml = temp_psumr;
        xl = temp_xr;
      }
    }
  }

  /* convert from x domain to radians */
  for (i = 0; i < order; i++) {
    freq[i] = acosf(freq[i]);
  }

  free(Q);
  free(P);
  return roots;
}

/*---------------------------------------------------------------------------*\

  FUNCTION....: lsp_to_lpc()
  AUTHOR......: David Rowe
  DATE CREATED: 24/2/93

  This function converts LSP coefficients to LPC coefficients.  In the
  Speex code we worked out a way to simplify this significantly.

\*---------------------------------------------------------------------------*/

void lsp_to_lpc(float *lsp, float *ak, int order)
/*  float *freq         array of LSP frequencies in radians     	*/
/*  float *ak 		array of LPC coefficients 			*/
/*  int order     	order of LPC coefficients 			*/
{
  int i, j;
  float xout1, xout2, xin1, xin2;
  float *pw, *n1, *n2, *n3, *n4 = 0;
  float *freq = (float *)malloc(order * sizeof(float));
  float *Wp = (float *)malloc(((order * 4) + 2) * sizeof(float));

  if (freq == NULL || Wp == NULL) {
    // Handle memory allocation failure
    free(freq);
    free(Wp);
    return;  // Or handle the error in another appropriate way
  }

  /* convert from radians to the x=cos(w) domain */
  for (i = 0; i < order; i++) freq[i] = cosf(lsp[i]);

  pw = Wp;
  /* initialise contents of array */
  for (i = 0; i <= 4 * (order / 2) + 1; i++) { /* set contents of buffer to 0 */
    *pw++ = 0.0f;
  }

  /* Set pointers up */
  pw = Wp;
  xin1 = 1.0f;
  xin2 = 1.0f;

  /* reconstruct P(z) and Q(z) by cascading second order polynomials
    in form 1 - 2xz(-1) +z(-2), where x is the LSP coefficient */
  for (j = 0; j <= order; j++) {
    for (i = 0; i < (order / 2); i++) {
      n1 = pw + (i * 4);
      n2 = n1 + 1;
      n3 = n2 + 1;
      n4 = n3 + 1;
      xout1 = xin1 - 2 * (freq[2 * i]) * (*n1) + *n2;
      xout2 = xin2 - 2 * (freq[2 * i + 1]) * (*n3) + *n4;
      *n2 = *n1;
      *n4 = *n3;
      *n1 = xin1;
      *n3 = xin2;
      xin1 = xout1;
      xin2 = xout2;
    }
    xout1 = xin1 + *(n4 + 1);
    xout2 = xin2 - *(n4 + 2);
    ak[j] = (xout1 + xout2) * 0.5f;
    *(n4 + 1) = xin1;
    *(n4 + 2) = xin2;
    xin1 = 0.0f;
    xin2 = 0.0f;
  }

  free(freq);
  free(Wp);
}