#!/bin/sh -e

echo "gcc"
g++ -g3 -O3 --std c++11 -Wall ir.cpp -o ir
g++ -g3 -O3 --std c++11 -Wall ir-ll.cpp -o ir-ll

sync
wait

./ir 250
./ir 350
./ir 450
wait

./ir-ll deque 250
./ir-ll deque 350
./ir-ll deque 450

echo
echo "clang"
clang++ -g3 -O3 --std c++11 -Wall ir.cpp -o ir
clang++ -g3 -O3 --std c++11 -Wall ir-ll.cpp -o ir-ll

sync
wait

./ir 250
./ir 350
./ir 450
wait

./ir-ll deque 250
./ir-ll deque 350
./ir-ll deque 450
wait

./ir-ll list 250
./ir-ll list 350
./ir-ll list 450
