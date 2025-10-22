#ifndef __BLOOM_H_TRANSPILATION__
#define __BLOOM_H_TRANSPILATION__
#include <bloom/parsing.h>

extern auto transpile_to_c(
    String *target_file_path,
    Array<ASTNode> *ast_nodes,
    ArenaAllocator *allocator
) -> void;

#endif // __BLOOM_H_TRANSPILATION__