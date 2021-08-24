# Chat Room


This project contains a chat room and a integration with Redis TB

[![Watch the video](https://img.youtube.com/vi/3qmcY8TxwWA/0.jpg)](https://youtu.be/3qmcY8TxwWA)

## Dependence

To run this project is necessary install the [Redis-cpp](https://github.com/tdv/redis-cpp) library and you'll need to install the boost

Redis-cpp install:

```
git clone https://github.com/tdv/redis-cpp.git  
cd redis-cpp
mkdir build  
cd build  
cmake ..  
make  
make install  
```

Bosst install:

```
sudo apt-get install libboost-all-dev
```

## Run the project

### Start Regis

First of all we will need to start the Redis DB server

for this enter in the default folder and run the redis-server

```
cd redis-stable/src/
./redis-server
```
The image below should be appear.

![Redis Star](img/Redis.png)

So Redis is running.

### Start Chatroom server

This repository contains the executable files.

To ruim this files enter in the folder 
