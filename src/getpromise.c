#include "vadr.h"

SEXP emptypromise() {
  SEXP out = PROTECT(allocSExp(PROMSXP));
  SET_PRCODE(out, R_MissingArg);
  SET_PRENV(out, R_EmptyEnv);
  SET_PRVALUE(out, R_UnboundValue);
  UNPROTECT(1);
  return out;
}

SEXP new_promise(SEXP expr, SEXP env) {
  SEXP out = PROTECT(allocSExp(PROMSXP));
  SET_PRCODE(out, expr);
  SET_PRENV(out, env);
  SET_PRVALUE(out, R_UnboundValue);
  UNPROTECT(1);
  return out;
}

SEXP new_forced_promise(SEXP expr, SEXP value) {
  SEXP out = PROTECT(allocSExp(PROMSXP));
  SET_PRCODE(out, expr);
  SET_PRENV(out, R_EmptyEnv);
  SET_PRVALUE(out, value);
  UNPROTECT(1);
  return out;
}

/* because this is not exposed in Rinternals.h for some reason */
SEXP do_ddfindVar(SEXP symbol, SEXP envir) {
  int i;
  SEXP vl;

  vl = findVar(R_DotsSymbol, envir);
  i = DDVAL(symbol);
  if (vl != R_UnboundValue) {
    if (length(vl) >= i) {
      vl = nthcdr(vl, i - 1);
      return(CAR(vl));
    }
    else
      error("the ... list does not contain %d elements", i);
  }
  else error("..%d used in an incorrect context, no ... to look in", i);

  return R_NilValue;
}

SEXP do_findBinding(SEXP name, SEXP envir) {
  assert_type(name, SYMSXP);
  assert_type(envir, ENVSXP);
  SEXP binding;
  if (DDVAL(name)) {
    binding = do_ddfindVar(name, envir);
  } else {
    binding = Rf_findVar(name, envir);
  }
  if (binding == R_UnboundValue) {
    error("Variable `%s` was not found.",
          CHAR(PRINTNAME(name)));
  }
  return binding;
}

SEXP do_findPromise(SEXP name, SEXP envir) {
  SEXP binding = do_findBinding(name, envir);
  if (binding == R_MissingArg) {
    binding = emptypromise();
  }
  if (TYPEOF(binding) != PROMSXP) {
    error("Variable `%s` was not bound to a promise",
          CHAR(PRINTNAME(name)));
  }
  return binding;
}

/* If not a promise, wrap in a promise. */
/* the arg_promise needs to also reflect changes in arg_env and arg_expr */
SEXP make_into_promise(SEXP in) {
  if (TYPEOF(in) == PROMSXP) {
    while (TYPEOF(PREXPR(in)) == PROMSXP) {
      in = PREXPR(in);
    }
    return in;
  } else {
    /* wrap in a promise */
    SEXP out = PROTECT(allocSExp(PROMSXP));
    SET_PRENV(out, R_EmptyEnv);
    SET_PRVALUE(out, in);
    SET_PRCODE(out, in);
    UNPROTECT(1);
    return out;
  }
}

/* Extract named variables from an environment into a dotslist */
SEXP _env_to_dots(SEXP envir, SEXP names, SEXP missing) {
  assert_type(envir, ENVSXP);
  assert_type(names, STRSXP);
  int use_missing = asLogical(missing);
  int length = LENGTH(names);
  SEXP out = R_NilValue;
  SEXP tail = R_NilValue;
  for (int i = 0; i < length; i++) {
    SEXP sym = install(CHAR(STRING_ELT(names, i)));
    SEXP found = findVar(sym, envir);
    if (found == R_UnboundValue) {
      error("Variable `%s` was not found.",
            CHAR(PRINTNAME(sym)));
    }
    /* check for missing variables */
    if (!use_missing) {
      SEXP unwrapped = found;
      while (TYPEOF(unwrapped) == PROMSXP)
        unwrapped = PRCODE(unwrapped);
      if (unwrapped == R_MissingArg) {
        continue;
      }
    }
    /* append new dotsxp */
    if (out == R_NilValue) {
      out = PROTECT(allocSExp(DOTSXP));
      tail = out;
    } else {
      SEXP new = allocSExp(DOTSXP);
      SETCDR(tail, new);
      tail = new;
    }
    /* descend into ... if that's what we have */
    if (sym == R_DotsSymbol && found != R_MissingArg) {
      assert_type(found, DOTSXP);
      while (1) {
        SEXP tag = TAG(found);
        SET_TAG(tail, tag);
        SETCAR(tail, CAR(found));
        found = CDR(found);
        if (found != R_NilValue) {
          SEXP new = allocSExp(DOTSXP);
          SETCDR(tail, new);
          tail = new;
        } else {
          break;
        }
      }
    } else {
      SET_TAG(tail, sym);
      SEXP made = make_into_promise(found);
      SETCAR(tail, made);
    }
  }
  if (out == R_NilValue) {
    out = PROTECT(allocVector(VECSXP, 0));
  };
  setAttrib(out, R_ClassSymbol, ScalarString(mkChar("...")));
  UNPROTECT(1);
  return out;
}

/* Add the entries in dots to the given environment;
 * if untagged, append to ... */
SEXP _dots_to_env(SEXP dots, SEXP envir) {
  assert_type(dots, DOTSXP);
  assert_type(envir, ENVSXP);
  SEXP newdots = R_NilValue;
  SEXP newdots_tail = newdots;
  for(SEXP iter = dots; iter != R_NilValue; iter = CDR(iter)) {
    if (TAG(iter) == R_NilValue) {
      /* no tag, so we store the value in "newdots" to be later assigned to ... */
      if (newdots == R_NilValue) {
        SEXP olddots = findVar(R_DotsSymbol, envir);
        if (olddots != R_UnboundValue) {
          /* there is already a binding for ..., so copy it to 'newdots'.*/
          assert_type(dots, DOTSXP);
          for (SEXP iter = olddots; iter != R_NilValue; iter = CDR(iter)) {
            SEXP copied = PROTECT(allocSExp(DOTSXP));
            SETCAR(copied, CAR(iter));
            SET_TAG(copied, TAG (iter));
            SETCDR(copied, R_NilValue);
            if (newdots == R_NilValue) {
              newdots = copied;
              newdots_tail = copied;
            } else {
              SETCDR(newdots_tail, copied);
              UNPROTECT(1);
              newdots_tail = copied;
            }
          }
        }
      }
      /* append new ... entry */
      SEXP new = PROTECT(allocSExp(DOTSXP));
      SETCAR(new, CAR(iter));
      SETCDR(new, R_NilValue);
      SET_TAG(new, R_NilValue);
      if (newdots == R_NilValue) {
        newdots = new;
        newdots_tail = new;
      } else {
        SETCDR(newdots_tail, new);
        UNPROTECT(1);
        newdots_tail = new;
      }
    } else {
      /* dots entry with tag, assigns to variable */
      defineVar(TAG(iter), CAR(iter), envir);
    }
  }
  if (newdots != R_NilValue) {
    defineVar(R_DotsSymbol, newdots, envir);
    UNPROTECT(1);
  }
  return envir;
}

/* selector for things arg_get finds */
typedef enum GET_ENUM {
  EXPR,
  ENV,
  PROMISE,
  IS_LITERAL, /* not a "test" because it follows logic of inspecting expr/value */
  IS_MISSING
} GET_ENUM;

const char* get_enum_string(GET_ENUM type) {
  switch(type) {
  case EXPR: return "expression";
  case ENV: return "environment";
  case PROMISE: return "promise";
  case IS_LITERAL: return "is literal";
  case IS_MISSING: return "is missing";
  }
}

/* selector for things arg_check finds */
typedef enum TEST_ENUM {
  IS_PROMISE,
  IS_FORCED
} TEST_ENUM;

const char* test_enum_string(TEST_ENUM type) {
  switch(type) {
  case IS_PROMISE: return "is promise";
  case IS_FORCED: return "is forced";
  }
}

SEXP arg_get(SEXP, SEXP, GET_ENUM, int);

SEXP arg_get_from_unforced_promise(SEXP prom, GET_ENUM request, int warn) {
  switch(request) {
  case EXPR: return PREXPR(prom);
  case ENV: return PRENV(prom);
  case PROMISE: return prom;
  case IS_LITERAL: return ScalarLogical(TRUE);
  case IS_MISSING:
    if (isSymbol(PREXPR(prom))) {
      if (PREXPR(prom) == R_MissingArg) {
        return ScalarLogical(TRUE);
      } else {
        /* as R's missing() does, recurse and chase missingness up... */
        return arg_get(PRENV(prom), PREXPR(prom), request, warn);
      }
    } else {
      return ScalarLogical(FALSE);
    }
  }
}

SEXP arg_get_from_forced_promise(SEXP sym, SEXP prom, GET_ENUM type, int warn) {
  SEXP expr = PREXPR(prom);
  switch(TYPEOF(expr)) {
  case INTSXP:                  /* plausible code literals */
  case STRSXP:
  case REALSXP:
    if (LENGTH(expr) > 1 || ATTRIB(expr) != R_NilValue) {
      if(warn) warning("`%s` already forced but non-scalar %s is expression.",
                       CHAR(PRINTNAME(sym)),
                       type2char(TYPEOF(expr)));
    }
    switch (type) {
    case EXPR: return expr;
    case ENV: return R_EmptyEnv;
    case PROMISE: return prom;
    case IS_LITERAL: return ScalarLogical(FALSE);
    case IS_MISSING: return ScalarLogical(FALSE);
    }

  case SYMSXP:                  /* can't fake an environment we don't have
                                   if the expr is not literal */
    if (expr == R_MissingArg) {
      /* except for the missing, which is a kind of literal */
      if(warn) warning("Argument `%s` is missing but also forced?!",
                      CHAR(PRINTNAME(sym)));
      switch(type) {
      case ENV: return R_EmptyEnv;
      case PROMISE: return emptypromise();
      case EXPR: return R_MissingArg;
      case IS_LITERAL: return ScalarLogical(TRUE);
      case IS_MISSING: return ScalarLogical(TRUE);
      }
    }
  case LANGSXP:
    switch(type) {
    case ENV:
      error("Argument `%s` already forced so cannot determine environment.",
            CHAR(PRINTNAME(sym)));
    case PROMISE:
      return prom;
    case EXPR:
      return expr;
    case IS_LITERAL:
      return ScalarLogical(FALSE);
    case IS_MISSING:
      return ScalarLogical(PRVALUE(prom) == R_MissingArg);
    }
  default:
    switch(type) {
    case ENV:
      return R_EmptyEnv;
    case EXPR:
      if(warn) warning("Argument `%s` already forced, %s found instead of expression?",
                      CHAR(PRINTNAME(sym)), type2char(TYPEOF(expr)));
      return expr;
    case PROMISE:
      return new_promise(expr, R_EmptyEnv);
    case IS_LITERAL:
      return ScalarLogical(FALSE);
    case IS_MISSING:
      return ScalarLogical(FALSE);
    }
  }
}

SEXP arg_get_from_nonpromise(SEXP sym, SEXP value, GET_ENUM request, int warn) {
  switch(TYPEOF(value)) {
  case INTSXP:                  /* plausible code literals */
  case STRSXP:
  case REALSXP:
    if (LENGTH(value) > 1 || ATTRIB(value) != R_NilValue) {
      /* warn about nonscalars */
      switch(request) {
      case EXPR:
      case ENV:
      case PROMISE:
        if(warn) warning("`%s` not a promise, bound to non-scalar %s instead.",
                CHAR(PRINTNAME(sym)),
                type2char(TYPEOF(value)));
      case IS_LITERAL:
      case IS_MISSING: break;
      }
    }
    switch(request) {              /* we have a numeric */
    case EXPR: return value;
    case ENV: return R_EmptyEnv;
    case PROMISE: return new_forced_promise(value, value);
    case IS_LITERAL: return ScalarLogical(TRUE);
    case IS_MISSING: return ScalarLogical(FALSE);
    }

  case SYMSXP:
    if (value == R_MissingArg) { /* Missingness is a code literal, and
                                  bytecompiler optimizes it similarly,
                                  unwrapping it from a promise. */
      switch (request) {
      case EXPR: return R_MissingArg;
      case ENV:
        if(warn) warning("`x` is missing with no environment recorded",
                         CHAR(PRINTNAME(sym)));
        return R_EmptyEnv;
      case PROMISE:
        return emptypromise();
      case IS_LITERAL:
        return ScalarLogical(TRUE);
      case IS_MISSING:
        return ScalarLogical(TRUE);
      }
    } else { /* a non missing symbol */
      switch(request) {
      case PROMISE:
      case ENV:
      case EXPR:
        error ("`%s` already forced and contains a symbol `%s`",
               CHAR(PRINTNAME(sym)),
               CHAR(PRINTNAME(value)));
        return R_NilValue;
      case IS_LITERAL:
        return ScalarLogical(FALSE);
      case IS_MISSING:
        return ScalarLogical(FALSE);
      }
    }
  case LANGSXP:
    switch(request) {
    case EXPR:
    case ENV:
    case PROMISE:
      error("`%s` already forced and contains a %s.",
            CHAR(PRINTNAME(sym)),
            type2char(TYPEOF(value)));
      return R_NilValue;
    case IS_LITERAL: return ScalarLogical(FALSE);
    case IS_MISSING: return ScalarLogical(FALSE);
    }
  default:
    if(warn) warning("`%s` not a promise, contains non-scalar %s.",
                    CHAR(PRINTNAME(sym)),
                    type2char(TYPEOF(value)));
    switch(request) {
    case ENV:
      return R_EmptyEnv;
    case EXPR:
      return value;
    case PROMISE:
      return new_forced_promise(value, value);
    case IS_LITERAL:
      return ScalarLogical(FALSE);
    case IS_MISSING:
      return ScalarLogical(FALSE);
    }
  }
}

SEXP arg_get(SEXP envir, SEXP name, GET_ENUM type, int warn) {
  /* Rprintf("Getting %s of binding `%s`\n", get_enum_string(type), CHAR(PRINTNAME(name))); */
  SEXP binding = do_findBinding(name, envir);
  if (TYPEOF(binding) == PROMSXP) {
    /* Rprintf("Got a promise\n"); */
    while (TYPEOF(PREXPR(binding))  == PROMSXP) {
      /* Rprintf("It's a wrapped promise\n"); */
      binding = PREXPR(binding);
    }
    if (PRVALUE(binding) != R_UnboundValue) {
      /* Rprintf("It's already forced\n"); */
      return arg_get_from_forced_promise(name, binding, type, warn);
    } else {
      /* Rprintf("It's unforced\n"); */
      return arg_get_from_unforced_promise(binding, type, warn);
    }
  } else {
    /* Rprintf("It's not a promise\n"); */
    return arg_get_from_nonpromise(name, binding, type, warn);
  }
}

SEXP arg_check(SEXP envir, SEXP name, TEST_ENUM type, int warn) {
  /* Rprintf("Getting %s of binding `%s`\n", */
  /*         test_enum_string(type), CHAR(PRINTNAME(name))); */
  SEXP binding = do_findBinding(name, envir);
  while (TYPEOF(binding) == PROMSXP && TYPEOF(PREXPR(binding)) == PROMSXP) {
    /* Rprintf("Got a wrapped promise\n"); */
    binding = PREXPR(binding);
  }
  if (TYPEOF(binding) == PROMSXP) {
    if (PRVALUE(binding) != R_UnboundValue) {
      switch (type) {
      case IS_PROMISE: return ScalarLogical(TRUE);
      case IS_FORCED: return ScalarLogical(TRUE);
      }
    } else {
      switch(type) {
      case IS_PROMISE: return ScalarLogical(TRUE);
      case IS_FORCED: return ScalarLogical(FALSE);
      }
    }
  } else {
      switch(type) {
      case IS_PROMISE: return ScalarLogical (FALSE);
      case IS_FORCED: return ScalarLogical (TRUE);
      }
  }
}

SEXP _arg_env(SEXP envir, SEXP name, SEXP warn) {
  return arg_get(envir, name, ENV, asLogical(warn));
}

SEXP _arg_expr(SEXP envir, SEXP name, SEXP warn) {
  return arg_get(envir, name, EXPR, asLogical(warn));
}

SEXP _arg_dots(SEXP envirs, SEXP names, SEXP tags, SEXP warn) {
  assert_type(envirs, VECSXP);
  assert_type(names, VECSXP);
  assert_type(tags, STRSXP);
  if (LENGTH(envirs) != LENGTH(names) || LENGTH(tags) != LENGTH(names)) {
    error("Inputs to arg_dots have different lengths");
  }
  int warni = asLogical(warn);

  int len = LENGTH(names);
  
  SEXP output = PROTECT(allocList(len));
  SEXP output_iter = output;
  for (int i = 0; i < len; i++, output_iter = CDR (output_iter)) {
    SET_TYPEOF(output_iter, DOTSXP);
    if ((tags != R_NilValue) && (STRING_ELT(tags, i) != R_BlankString)) {
      SET_TAG(output_iter, install(CHAR(STRING_ELT(tags, i))));
    }
    SEXP promise =
      arg_get(VECTOR_ELT(envirs, i), VECTOR_ELT(names, i), PROMISE, warni);
    SETCAR(output_iter, promise);
  }
  setAttrib(output, R_ClassSymbol, ScalarString(mkChar("...")));
  UNPROTECT(1);
  return(output);
}

SEXP _is_promise(SEXP envir, SEXP name, SEXP warn) {
  return arg_check(envir, name, IS_PROMISE, asLogical(warn));
}

SEXP _is_forced(SEXP envir, SEXP name, SEXP warn) {
  return arg_check(envir, name, IS_FORCED, asLogical(warn));
}

SEXP _is_literal(SEXP envir, SEXP name, SEXP warn) {
  return arg_get(envir, name, IS_LITERAL, asLogical(warn));
}

SEXP _is_missing(SEXP envir, SEXP name, SEXP warn) {
  return arg_get(envir, name, IS_MISSING, asLogical(warn));
}

/*
 * Local Variables:
 * eval: (previewing-mode)
 * previewing-build-command: (previewing-run-R-unit-tests)
 * End:
 */
