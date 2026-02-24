target_blm=calc
cmd="\
    cd build && \
    cmake --build . && \
    clear && \
    ./bloomc run ../docs/examples/$target_blm.blm && \
    cd .. && \
    printf '\n--- Output of generated C program ---\n' && \
    gcc -o sum sum.c && ./sum
"
find -name '*.h' -or -name '*.cpp' | entr bash -c "sleep 0.25 && $cmd"
