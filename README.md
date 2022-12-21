a simple tcp tunnel.
I use this stuff to forward my Terraria Server😝

# build
build with cmake:
```shell
mkdir build
cd build
cmake ..
make
```

or, just compile it use `g++`:
```shell
g++ utils.cpp hub.cpp -o hub -lboost_program_options -pthread
g++ utils.cpp ep.cpp -o ep -lboost_program_options -pthread
```
# Usage

run `hub` in svr, which you can connect anywhere.
and run `ep` in where you want to forward.

for example, I had a game svr in my local pc with port `33333`.
my friends want to connect it in maybe another city.
then I run:

```shell
# in my server
./hub -e 11111 -p 22222 -c 33333  --psw 123

# in my pc where i run game svr
./ep -u x.x.x.x -p 11111 -l 33333 --psw 123

# my friends
# just connect to x.x.x.x:33333 in game
```
then they could enjoy the game with me.
