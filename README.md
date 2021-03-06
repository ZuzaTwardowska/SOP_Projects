## Overview
This repository consists of four seperate projects:
- [Graph simulation with pipe and fifo](#graph)
- [Message Queues](#message)
- [Quiz with TCP](#quiz)
- [Producer-consumer with shared memory and semaphores](#producer-consumer)


## Graph simulation with pipe and fifo

Program consists of the management process and verticles' processes. Communication between processes
is implemented with pipes. Fifo file is used by the management process to communicate with user that can execute commands:
- print - prints all of the edges in the graph
- add x y - adds (directed) edge from verticle x to verticle y
- conn x y - returns information whether there exists any connection from verticle x to verticle y (direct or indirect)

## Message Queues

Generator creates (or uses existing) message queues q1 and q2. If queues does not exist and need to be created, it publishes 
to q1 n (optional argument) messages with its pid and 3 random characters. If argument n was given but queues with given names exist,
generator ends with an error. During its cycle, generator reads messages from q1 and generates a new message by adding five 
random characters to the original message. After t seconds of sleep, the new message is sent to q2 with p% likelihood and the 
original message is sent back to q1.

Processor reades messages from q2, sleeps for t seconds, and prints out the message. Then it changes the message by replacing the pid with its
own pid and first three random characters with '000' (for example 1234/abc/hjklm => 5678/000/hjklm). The changed message is sent back to q2
with p% likelihood.

The messages generated by the generator have a higher priority for processor then those genereated by processor programs.
Multiple generators and processors may use the same message queues.
Processors waits for messages at most one second - if no messages have been read during this time, processor prints out 
the latest message it has read.

## Quiz with TCP

Server is run with address, port, path to the file with questions and maximum number of clients given as the arguments.
Server reads all of the questions from the file and accepts clients on the given port and address. If number of the connected clients
exceeds the number of maximum clients, it responds to the client waiting for the connection with refusal.
Server sends parts of the question to each of the connected clients three times per second. For every client server 
randomizes the number of characters to send in the current transfer. If no question is "in progress", it also randomizes which question is going
to be send to the certain client. If all characters of the question have been sent to the client, server waits with sending the next question
until it receives a response. Server ends with a SIGINT.

Client is run with <port, address> pairs of arguments for each server it is going to contact. Client receives parts of the questions
from each server. When the whole question has been received, it is printed and program waits for user's answer that is later sent to the server.
If user tries to enter an answer before the question has been received completely, it prints out a reminder that user should wait for the question.
Client program ends when all of the servers end connection.
If a question from the other server is completed while client is waiting for the user's input, client sends to the first server a '0' response
and proceeds with the next question.

## Producer-consumer with shared memory and semaphores

Program can work in two modes: -p producer or -k consumer. Program instances communicate with each other via 10 element array of strings in shared memory.
Synchronization is achieved with semaphores.

Producers read messages from TCP clients in seperate threads and places them in the shared memory. When the message is places in the
shared memory, one of the consumers reads the message and rotates it. Then the rotated message is put back in the same place in the shared memory.
When the rotated message is returned, one of the producers reads and prints out the message.

Multiple producers and consumers may work with the same shared memory due to synchonization. Program is controlled with SIGINT.

## Technologies
- C
- Linux
