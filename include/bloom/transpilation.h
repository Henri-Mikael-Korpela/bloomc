#ifndef __BLOOM_H_TRANSPILATION__
#define __BLOOM_H_TRANSPILATION__
#include <bloom/parsing.h>

extern auto transpile_to_c(Array<ASTNode> *ast_nodes, ArenaAllocator *allocator) -> Str;

#endif // __BLOOM_H_TRANSPILATION__