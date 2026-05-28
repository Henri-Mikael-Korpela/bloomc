#pragma once
#include <bloom/parsing.h>

auto transpile_to_c(Array<ASTNode> *ast_nodes, ArenaAllocator *allocator) -> Str;
