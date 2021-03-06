// Interpreter and runtime for Futaba programming languange.
// Created by liftA42 on Nov 17, 2017.
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gc.h"
#define malloc GC_malloc
#define realloc GC_realloc

// Part 0, preparation
enum Error {
  ERROR_UNRESOLVED_NAME = 1,
  ERROR_CANNOT_OPEN_FILE,
  ERROR_NO_ARGV,
  ERROR_UNRECOGNIZED_SYMBOL,
  ERROR_UNCOMPLETED_SENTENCE,
  ERROR_RECURSIVE_SELF
};

#define SYNTAX_SENTENCE_END '.'
#define SYNTAX_LAMBDA_HEAD '`'
#define SYNTAX_SENTENCT_BREAK ','
#define SYNTAX_COMMENT_HEAD ';'

// `Piece` structure
// Everything in Futaba is a `Piece`.
struct _Piece;

typedef struct _Piece *(*PieceFunc)(struct _Piece *, void *);

struct _Piece {
  PieceFunc function;
  void *backpack;
};

typedef struct _Piece Piece;

// generalized creator of `Piece`
Piece *piece_create(PieceFunc func, void *pack) {
  Piece *piece = malloc(sizeof(Piece));
  piece->function = func;
  piece->backpack = pack;
  return piece;
}

// specific creator of common C types
Piece *internal_self(Piece *, void *);

#define PIECE_CREATE_GENERATE(type)                                            \
  Piece *piece_create_##type(type val) {                                       \
    type *p = malloc(sizeof(type));                                            \
    *p = val;                                                                  \
    return piece_create(internal_self, p);                                     \
  }

PIECE_CREATE_GENERATE(int)
PIECE_CREATE_GENERATE(bool)

typedef struct {
  Piece *caller;
  Piece *callee;
} BackpackCall;

Piece *internal_call(Piece *, void *);

Piece *piece_create_call(Piece *caller, Piece *callee) {
  BackpackCall *backpack = malloc(sizeof(BackpackCall));
  backpack->caller = caller;
  backpack->callee = callee;
  return piece_create(internal_call, backpack);
}

// Part 1, parser
// `Record` structure
// Compile (parse) time helper for linking names and `Piece`s.
typedef struct _Record {
  char *name;
  int name_len;
  Piece *piece;
  struct _Record *previous;
} Record;

Piece *record_resolve(Record *record, char *name, int name_len) {
  if (record == NULL) {
    // there's no enough info about source code, so let parent to raise error
    return NULL;
  }
  if (name_len == record->name_len &&
      strncmp(record->name, name, name_len) == 0) {
    return record->piece;
  }
  return record_resolve(record->previous, name, name_len);
}

Record *record_register(Record *pre, char *name, int name_len, Piece *piece) {
  Record *record = malloc(sizeof(Record));
  record->name = name;
  record->name_len = name_len;
  record->piece = piece;
  record->previous = pre;
  return record;
}

// `Source` structure
// A `Source` represents a source code file.
typedef struct {
  char *file_name;
  char *source;
  int length;
  int current;
  int line;
  int column;
} Source;

char source_fetch(Source *s) {
  return s->current == s->length ? EOF : s->source[s->current];
}

bool source_forward(Source *s) {
  if (s->current < s->length) {
    if (s->source[s->current] == '\n') {
      s->line++;
      s->column = 1;
    } else {
      s->column++;
    }
    s->current++;
  }
  return s->current != s->length;
}

Source *source_create(char *s, int len, char *file_name) {
  Source *source = malloc(sizeof(Source));
  source->source = s;
  source->file_name = file_name;
  source->length = len;
  source->current = 0;
  source->line = source->column = 1;
  return source;
}

// parser utils
char *parse_cover_name(Source *source, int *name_len) {
  char *name = &source->source[source->current];
  *name_len = 0;
  do {
    source_forward(source);
    (*name_len)++;
  } while (isgraph(source_fetch(source)) &&
           source_fetch(source) != SYNTAX_SENTENCE_END &&
           source_fetch(source) != SYNTAX_SENTENCT_BREAK);
  return name;
}

#define PARSE_GRUMBLE(source, message...)                                      \
  fprintf(stderr, message);                                                    \
  fprintf(stderr, "%s:%d:%d\n", source->file_name, source->line,               \
          source->column);

Piece *parse_aggregate_call(Piece *acc, Piece *item) {
  return acc == NULL ? item : piece_create_call(acc, item);
}

// parser components
Piece *parse_int(Source *source) {
  int num = 0;
  do {
    num = num * 10 + (source_fetch(source) - '0');
    if (!source_forward(source)) {
      break;
    }
  } while (isdigit(source_fetch(source)));
  return piece_create_int(num);
}

Piece *parse_name(Source *source, Record *record) {
  int name_len;
  char *name = parse_cover_name(source, &name_len);
  Piece *piece = record_resolve(record, name, name_len);
  if (piece == NULL) {
    PARSE_GRUMBLE(source, "unresolved name \"%.*s\" near ", name_len, name);
    exit(ERROR_UNRESOLVED_NAME);
  }
  return piece;
}

Piece *parse_sentence(Source *, Record *);

typedef struct {
  Piece *body;
  Piece *arg;
} BackpackLambda;

Piece *internal_lambda(Piece *, void *);
Piece *internal_argument(Piece *, void *);

Piece *parse_lambda(Source *source, Record *record) {
  source_forward(source);

  int name_len;
  char *name = parse_cover_name(source, &name_len);

  // `hold` will be filled per invoking
  Piece *hold = piece_create(NULL, NULL);
  Record *r = record_register(record, name, name_len, hold);

  BackpackLambda *backpack = malloc(sizeof(BackpackLambda));
  backpack->body = parse_sentence(source, r);
  backpack->arg = hold;
  return piece_create(internal_lambda, backpack);
}

Piece *parse_piece(Source *source, Record *record) {
  if (isdigit(source_fetch(source))) {
    return parse_int(source);
  } else if (source_fetch(source) == SYNTAX_LAMBDA_HEAD) {
    return parse_lambda(source, record);
  } else if (isgraph(source_fetch(source))) {
    return parse_name(source, record);
  } else {
    PARSE_GRUMBLE(source, "unrecognized symbol near \'0x%x\' at ",
                  source_fetch(source) & 0xff);
    exit(ERROR_UNRECOGNIZED_SYMBOL);
  }
}

Piece *parse_sentence_impl(Source *source, Record *record, Piece *result) {
  while (isspace(source_fetch(source))) {
    source_forward(source);
  }

  switch (source_fetch(source)) {
  case SYNTAX_COMMENT_HEAD:
    do {
      source_forward(source);
    } while (source_fetch(source) != '\n');
    return parse_sentence_impl(source, record, result);
  case SYNTAX_SENTENCE_END:
    source_forward(source);
    return result;
  case EOF:
    PARSE_GRUMBLE(source, "uncompleted sentence in ");
    exit(ERROR_UNCOMPLETED_SENTENCE);
  case SYNTAX_SENTENCT_BREAK:
    source_forward(source);
    return parse_aggregate_call(result,
                                parse_sentence_impl(source, record, NULL));
  default:
    return parse_sentence_impl(
        source, record,
        parse_aggregate_call(result, parse_piece(source, record)));
  }
}

Piece *parse_sentence(Source *source, Record *record) {
  return parse_sentence_impl(source, record, NULL);
}

// Part 2, apply
Piece *apply(Piece *caller, Piece *callee) {
  return caller->function(callee, caller->backpack);
}

// Part 3, internals
Piece *internal_self(Piece *callee, void *backpack) {
  if (callee->function == internal_self) {
    fprintf(stderr,
            "recursive `self` calling between two pieces\n"
            "piece 1 backpack: %p\n"
            "piece 2: %p backpack: %p",
            backpack, callee, callee->backpack);
    exit(ERROR_RECURSIVE_SELF);
  }
  return apply(callee, piece_create(internal_self, backpack));
}

Piece *internal_call(Piece *callee, void *backpack) {
  BackpackCall *pack = backpack;
  return apply(apply(pack->caller, pack->callee), callee);
}

Piece *internal_lambda(Piece *callee, void *backpack) {
  BackpackLambda *pack = backpack;
  memcpy(pack->arg, callee, sizeof(Piece));
  return pack->body;
}

Piece *internal_put(Piece *callee, void *backpack) {
  int *p_char = callee->backpack;
  putchar(*p_char);
  return piece_create(internal_self, NULL);
}

typedef struct {
  int i1;
  int i2;
} BackpackOp2;

#define INTERNAL_GENERATE_OP(name, op, type)                                   \
  Piece *internal_##name##_2(Piece *, void *);                                 \
                                                                               \
  Piece *internal_##name(Piece *callee, void *backpack) {                      \
    return piece_create(internal_##name##_2, callee->backpack);                \
  }                                                                            \
                                                                               \
  Piece *internal_##name##_3(Piece *, void *);                                 \
                                                                               \
  Piece *internal_##name##_2(Piece *callee, void *backpack) {                  \
    int *i1 = backpack, *i2 = callee->backpack;                                \
    BackpackOp2 *pack = malloc(sizeof(BackpackOp2));                           \
    pack->i1 = *i1;                                                            \
    pack->i2 = *i2;                                                            \
    return piece_create(internal_##name##_3, pack);                            \
  }                                                                            \
                                                                               \
  Piece *internal_##name##_3(Piece *callee, void *backpack) {                  \
    BackpackOp2 *pack = backpack;                                              \
    return apply(callee, piece_create_##type(pack->i1 op pack->i2));           \
  }

INTERNAL_GENERATE_OP(add, +, int)
INTERNAL_GENERATE_OP(sub, -, int)
INTERNAL_GENERATE_OP(mul, *, int)
INTERNAL_GENERATE_OP(div, /, int)
INTERNAL_GENERATE_OP(lt, <, bool)
INTERNAL_GENERATE_OP(eq, ==, bool)

Piece *internal_end(Piece *callee, void *backpack) { return NULL; }

Piece *internal_if_2(Piece *, void *);

Piece *internal_if(Piece *callee, void *backpack) {
  return piece_create(internal_if_2, callee->backpack);
}

typedef struct {
  Piece *left;
  bool cond;
} BackpackIf2;

Piece *internal_if_3(Piece *, void *);

Piece *internal_if_2(Piece *callee, void *backpack) {
  bool *p_b = backpack;
  BackpackIf2 *pack = malloc(sizeof(BackpackIf2));
  pack->left = callee;
  pack->cond = *p_b;
  return piece_create(internal_if_3, pack);
}

Piece *internal_if_3(Piece *callee, void *backpack) {
  BackpackIf2 *pack = backpack;
  return pack->cond ? pack->left : callee;
}

// Part 4, driver
Source *main_create_source(char *file_name) {
  FILE *source_file = fopen(file_name, "r");
  if (!source_file) {
    fprintf(stderr, "cannot open file \"%s\"\n", file_name);
    exit(ERROR_CANNOT_OPEN_FILE);
  }

  char *source = NULL;
  int length = 0;
  while (true) {
    char *line = NULL;
    size_t trivial_length = 0;
    int line_len = getline(&line, &trivial_length, source_file);
    if (line_len < 0) {
      break;
    }
    if (source == NULL) {
      source = malloc(sizeof(char) * (line_len + 1));
      strcpy(source, line);
    } else {
      source = realloc(source, sizeof(char) * (length + line_len + 1));
      strcat(source, line);
    }
    length += line_len;
  }
  fclose(source_file);
  return source_create(source, length, file_name);
}

#define MAIN_REGISTER_INTERNAL(record, name, func)                             \
  record = record_register(record, name, sizeof(name) - 1,                     \
                           piece_create(func, NULL));

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "please specify source file by command line argument\n");
    exit(ERROR_NO_ARGV);
  }

  GC_init();

  Record *r = NULL;
  MAIN_REGISTER_INTERNAL(r, "put", internal_put);
  MAIN_REGISTER_INTERNAL(r, "+", internal_add);
  MAIN_REGISTER_INTERNAL(r, "-", internal_sub);
  MAIN_REGISTER_INTERNAL(r, "*", internal_mul);
  MAIN_REGISTER_INTERNAL(r, "/", internal_div);
  MAIN_REGISTER_INTERNAL(r, "<", internal_lt);
  MAIN_REGISTER_INTERNAL(r, "=", internal_eq);
  MAIN_REGISTER_INTERNAL(r, "?", internal_if);
  MAIN_REGISTER_INTERNAL(r, "nil", internal_self);

  apply(parse_sentence(main_create_source(argv[1]), r),
        piece_create(internal_end, NULL));
}
