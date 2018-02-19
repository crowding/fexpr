#ifndef _VADR_H
#define _VADR_H

#include <R.h>
#include <Rinternals.h>

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))
#define MIN3(x, y, z) (MIN(x,(MIN(y,z))))
#define MAX3(x, y, z) (MAX(x,(MAX(y,z))))

void assert_type(SEXP, SEXPTYPE);
void assert_type3(SEXP, SEXPTYPE, const char *);
int recycle_length(int i, int j);
SEXP allocate_dots(int length);

SEXP emptypromise();
SEXP new_promise(SEXP expr, SEXP env);
SEXP new_forced_promise(SEXP expr, SEXP value);

#endif
