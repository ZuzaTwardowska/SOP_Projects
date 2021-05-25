## Overview
This repository consists of four seperate projects:
- [Graph simulation with pipe and fifo](#graph)
- [Message Queues](#message)
- [Quiz with TCP](#quiz)
- 


## Graph simulation with pipe and fifo

Program consists of the management process and verticles' processes. Communication between processes
is implemented with pipes. Fifo file is used by the management process to communicate with user that can execute commands:
- print - prints all of the edges in the graph
- add x y - adds (directed) edge from verticle x to verticle y
- conn x y - returns information whether there exists any connection from verticle x to verticle y (direct or indirect)

## Message Queues

## Quiz with TCP

## Technologies
- C
- Linux
