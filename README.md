# tunnel

## Hub + Endpoint(ep)
client(user) --------------------> Svr
client(user) -> Hub <- Endpoint -> Svr
Exp:
player(any net) -> Hub(cloud) <- Ep|Svr (local)

a connection creation timeline:
1. Hub start, listen for ep
2. Ep start, connect to Hub
3. client -(tcp)-> cp Hub
4. Hub ep -(new connection pls)-> Ep
5. Ep connect Svr, Ep connect Hub, connect the two connection
6. Hub rcv connection from Ep, connect client connection(step 3) and this connection
7. user & Svr communicate

## Problem

### detect socket close
6 socket in a full tunnel, name them as:
Client(c) <-> (hc) Hub (he) <-> (eh) Ep (es) <-> (s) Svr
#### user close conn
user close c. how to detect it.
#### svr close conn
Svr close s, how to detect it.

## Usage
### svr
```shell
./svr -p 9999 -t 1111:2222 -u 3333:4444
```

### hub+ep

### hub+ep (ver.2)
remove connection pool

### hub (ver.3)
queue connection from ep

### hub(ver4) ep(ver3)
added ip:port record

add reconnect between hub & ep

seperate ep:epc

add epc expire

add psw