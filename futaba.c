// Interpreter and runtime for Futaba programming languange.
// Created by liftA42 on Nov 17, 2017.
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Part 0, preparation
enum Error {
  ERROR_UNRESOLVED_NAME = 1,
  ERROR_CANNOT_OPEN_FILE,
  ERROR_NO_ARGV,
  ERROR_UNRECOGNIZED_SYMBOL
};

// `Piece` structure
// Everything in Futaba is a `Piece`.
struct _Piece;
typedef struct _Piece *(*PieceFunc)(struct _Piece *, void *);

struct _Piece {
  PieceFunc function;
  void *backpack;
};
typedef struct _Piece Piece;

Piece *piece_create(PieceFunc func, void *pack) {
  Piece *piece = malloc(sizeof(Piece));
  piece->function = func;
  piece->backpack = pack;
  return piece;
}

Piece *internal_self(Piece *, void *);
Piece *piece_create_int(int num) {
  int *p_int = malloc(sizeof(int));
  *p_int = num;
  return piece_create(internal_self, p_int);
}

Piece *piece_create_bool(bool b) {
  bool *p_b = malloc(sizeof(bool));
  *p_b = b;
  return piece_create(internal_self, p_b);
}

// Part 1, parser
// `Table` structure
// Compile (parse) time helper for linking names and `Piece`s.
typedef struct {
  char *name;
  int name_len;
  Piece *piece;
} Record;

typedef struct _Table {
  Record **records;
  int size; // varied
  int length;
  struct _Table *upper;
} Table;

#define TABLE_INIT_SIZE 16
Table *table_create(Table *upper) {
  Table *table = malloc(sizeof(Table));
  table->records = malloc(sizeof(Record *) * TABLE_INIT_SIZE);
  table->size = TABLE_INIT_SIZE;
  table->length = 0;
  table->upper = upper;
  return table;
}

Piece *table_resolve(Table *table, char *name, int name_len) {
  printf("resolving name %.*s in table %p\n", name_len, name, table);
  if (table == NULL) {
    fprintf(stderr, "unresolved name: %.*s\n", name_len, name);
    exit(ERROR_UNRESOLVED_NAME);
  }
  for (int i = 0; i < table->length; i++) {
    if (name_len != table->records[i]->name_len) {
      continue;
    }
    if (strncmp(table->records[i]->name, name, name_len) == 0) {
      printf("name: %.*s piece: %p\n", name_len, name,
             table->records[i]->piece);
      return table->records[i]->piece;
    }
  }
  return table_resolve(table->upper, name, name_len);
}

void table_register(Table *table, char *name, int name_len, Piece *piece) {
  if (table->length == table->size) {
    table->size *= 2;
    table->records = realloc(table, sizeof(Record *) * table->size);
  }
  Record *record = malloc(sizeof(Record));
  record->name = name;
  record->name_len = name_len;
  record->piece = piece;
  table->records[table->length] = record;
  table->length++;
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
  if (s->source[s->current] == '\n') {
    s->line++;
    s->column = 0;
  } else {
    s->column++;
  }
  s->current++;
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

Piece *parse_piece(Source *source, Table *table) {
  if (isdigit(source_fetch(source))) {
    return parse_int(source);
  } else if (isprint(source_fetch(source))) {
    char *name = source->source + source->current;
    int name_len = 0;
    do {
      source_forward(source);
      name_len++;
    } while (isprint(source_fetch(source)) && source_fetch(source) != ' ');
    return table_resolve(table, name, name_len);
  } else {
    fprintf(stderr, "unrecognized symbol near \'0x%x\' at %s:%d:%d\n",
            source_fetch(source) & 0xff, source->file_name, source->line,
            source->column);
    exit(ERROR_UNRECOGNIZED_SYMBOL);
  }
}

typedef struct {
  Piece *caller;
  Piece *callee;
} BackpackCall;
Piece *internal_call(Piece *, void *);
Piece *parse_impl(Source *, Table *, Piece *);
Piece *parse(Source *source, Table *table) {
  Piece *result = NULL;
  while (source_fetch(source) != EOF) {
    Piece *line = parse_impl(source, table, NULL);
    if (result == NULL) {
      result = line;
    } else {
      if (line != NULL) {
        BackpackCall *backpack = malloc(sizeof(BackpackCall));
        backpack->caller = result;
        backpack->callee = line;
        result = piece_create(internal_call, backpack);
      }
    }
    source_forward(source);
  }
  return result;
}

Piece *parse_impl(Source *source, Table *table, Piece *result) {
  while (source_fetch(source) == ' ') {
    source_forward(source);
  }
  if (source_fetch(source) == '\n' || source_fetch(source) == EOF) {
    return result;
  }

  if (result == NULL) {
    return parse_impl(source, table, parse_piece(source, table));
  } else {
    BackpackCall *backpack = malloc(sizeof(BackpackCall));
    backpack->caller = result;
    backpack->callee = parse_piece(source, table);
    return parse_impl(source, table, piece_create(internal_call, backpack));
  }
}

// Part 2, apply
void *apply(Piece *caller, Piece *callee) {
  printf("apply caller func %p callee func %p\n", caller->function, callee->function);
  return caller->function(callee, caller->backpack);
}

// Part 3, internals
Piece *internal_self(Piece *callee, void *backpack) {
  printf("calling `self` with callee %p\n", callee);
  return apply(callee, piece_create(internal_self, backpack));
}

Piece *internal_call(Piece *callee, void *backpack) {
  printf("calling `call` with callee %p\n", callee);
  BackpackCall *pack = backpack;
  return apply(apply(pack->caller, pack->callee), callee);
}

Piece *internal_put(Piece *callee, void *backpack) {
  printf("calling `put` with callee %p\n", callee);
  int *p_char = callee->backpack;
  putchar(*p_char);
  putchar('\n');
  return piece_create(internal_self, NULL);
}

Piece *internal_add_2(Piece *, void *);
Piece *internal_add(Piece *callee, void *backpack) {
  printf("calling `add` with callee %p\n", callee);
  return piece_create(internal_add_2, callee->backpack);
}

Piece *internal_add_2(Piece *callee, void *backpack) {
  printf("calling `add_2` with callee %p\n", callee);
  int *i1 = backpack, *i2 = callee->backpack;
  return piece_create_int(*i1 + *i2);
}

Piece *internal_end(Piece *callee, void *backpack) {
  printf("calling `end` with callee %p\n", callee);
  return NULL;
}

Piece *internal_lt_2(Piece *callee, void *backpack);
Piece *internal_lt(Piece *callee, void *backpack) {
  printf("calling `lt` with callee %p\n", callee);
  return piece_create(internal_lt_2, callee->backpack);
}

Piece *internal_lt_2(Piece *callee, void *backpack) {
  printf("calling `lt_2` with callee %p\n", callee);
  int *i1 = backpack, *i2 = callee->backpack;
  return piece_create_bool(*i1 < *i2);
}

Piece *internal_if_2(Piece *, void *);
Piece *internal_if(Piece *callee, void *backpack) {
  printf("calling `if` with callee %p\n", callee);
  return piece_create(internal_if_2, callee->backpack);
}

typedef struct {
  Piece *left;
  bool cond;
} BackpackIf2;
Piece *internal_if_3(Piece *, void *);
Piece *internal_if_2(Piece *callee, void *backpack) {
  printf("calling `if_2` with callee %p\n", callee);
  bool *p_b = backpack;
  BackpackIf2 *pack = malloc(sizeof(BackpackIf2));
  pack->left = callee;
  pack->cond = *p_b;
  return piece_create(internal_if_3, pack);
}

Piece *internal_if_3(Piece *callee, void *backpack) {
  printf("calling `if_3` with callee %p\n", callee);
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
    int line_len = 0;
    line_len = (int)getline(&line, (size_t *)&line_len, source_file);
    if (line_len < 0) {
      break;
    }
    if (source == NULL) {
      source = line;
    } else {
      source = realloc(source, sizeof(char) * (length + line_len + 1));
      strcat(source, line);
    }
    length += line_len;
  }
  fclose(source_file);
  return source_create(source, length, file_name);
}

#define MAIN_REGISTER_INTERNAL(table, name, func, backpack)                    \
  table_register(table, name, sizeof(name) - 1, piece_create(func, backpack))

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "please specify source file by command line argument\n");
    exit(ERROR_NO_ARGV);
  }

  Table *table = table_create(NULL);
  MAIN_REGISTER_INTERNAL(table, "put", internal_put, NULL);
  MAIN_REGISTER_INTERNAL(table, "+", internal_add, NULL);
  MAIN_REGISTER_INTERNAL(table, "<", internal_lt, NULL);
  MAIN_REGISTER_INTERNAL(table, "if", internal_if, NULL);

  Piece *p = parse(main_create_source(argv[1]), table);
  apply(p, piece_create(internal_end, NULL));
}
