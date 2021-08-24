#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>
#include <atomic>
#include <sstream>

#include <redis-cpp/stream.h>
#include <redis-cpp/execute.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048

std::atomic<unsigned int > cli_count;
static int uid = 10;

int  idActualMessage = 0;
auto redisConnection = rediscpp::make_stream("localhost", "6379");
int  numberUsers =0;


/* Client structure */
typedef struct{
	struct sockaddr_in address;
	int sockfd;
	int uid;
	char name[32];
} client_t;


client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void str_overwrite_stdout() {
    printf("\r%s", "> ");
    fflush(stdout);
}

void str_trim_lf (char* arr, int length) {
  	int i;
  	for (i = 0; i < length; i++) {
    if (arr[i] == '\n') {
		arr[i] = '\0';
      	break;
    }
  }
}

void print_client_addr(struct sockaddr_in addr){
    printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

/* Add clients to queue */
void queue_add(client_t *cl){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(!clients[i]){
			clients[i] = cl;
			break;
		}
	}

	pthread_mutex_unlock(&clients_mutex);
}

/* Remove clients to queue */
void queue_remove(int uid){
	pthread_mutex_lock(&clients_mutex);

	for(int i=0; i < MAX_CLIENTS; ++i){
		if(clients[i]){
			if(clients[i]->uid == uid){
				clients[i] = NULL;
				break;
			}
		}
	}
	pthread_mutex_unlock(&clients_mutex);
}

void broadcastMessage(const char* message){

	for(int i=0; i<MAX_CLIENTS; ++i){
		if(clients[i] && strlen(message)!= 0){
			write(clients[i]->sockfd,message,strlen(message));
		}
	}
}

/* send last messages*/
void sendLastMessages(void){
	pthread_mutex_lock(&clients_mutex);

		int oldMessage = idActualMessage-20;
		while(oldMessage < 0)
			oldMessage++;
		for(; oldMessage < idActualMessage; oldMessage++)
		{
			try{
				// GET FROM REDIS
				auto const oldMessageStr = std::to_string(oldMessage);		// convert oldMessage to string
				rediscpp::execute_no_flush(*redisConnection, "get", oldMessageStr);	// get in redis
				std::flush(*redisConnection);
				rediscpp::value value{*redisConnection};

				// CONVERSION TO SEND TO WRITE()
				auto messageReturned = value.as<std::string>(); 			// move value returned to "messageReturned"
				int n = messageReturned.size();					

				char messageReturnedChar[n+1];								// convert messageReturned to char
				strcpy(messageReturnedChar, messageReturned.c_str());

				char const *idChar = std::to_string(oldMessage).c_str();
				broadcastMessage(idChar);
				broadcastMessage(" -> ");
				broadcastMessage(messageReturnedChar);
			}
			catch(std::exception const&){}
	  	}
	pthread_mutex_unlock(&clients_mutex);
}

void printHeader(void)
{
	const char * header = "== COCUS CHAT ROOM ==\n\n";
	broadcastMessage(header);
}

/* Clear users screen*/
void clearScreen(void){
	pthread_mutex_lock(&clients_mutex);
	const char* clearMessage = "system-clear";
	broadcastMessage(clearMessage);
	usleep(100000); 								// prevents no clear screen, but add delay 
	pthread_mutex_unlock(&clients_mutex);
}

/* Save em Redis*/
 void setRedis(char * message){
	std::string messageSet(message);							// execute() needs all in string
	auto const idActualMessageString = std::to_string(idActualMessage);				// so lets convert int and char to string
	static_cast<void>(rediscpp::execute(*redisConnection, "set", idActualMessageString, messageSet));
	// delete oldest message "21"
 	if(idActualMessage >19){
		auto const itemDel = std::to_string(idActualMessage-20); 
		static_cast<void>(rediscpp::execute(*redisConnection,"del", itemDel));
 	}
 	idActualMessage++;
 	clearScreen();
	printHeader();
 	sendLastMessages();
 }

 /*get message's owner*/
 std::string getMessageOwner(int idMessage){
	// get message
	static_cast<void>(rediscpp::execute_no_flush(*redisConnection, "get", std::to_string(idMessage)));
	std::flush(*redisConnection);
	rediscpp::value value{*redisConnection};
	// separe user that sended the message
	auto messageReturned = value.as<std::string>();
	size_t operator_position = messageReturned.find_first_of(":");
	std::string messageOwner = messageReturned.substr(0, operator_position);
	// return message's owner
	return messageOwner;
 }

/* Remove message */
void remove_message(std::string user,std::string command)
{	
	std::string numberDeleted = command.substr(5);

	// conversion to fix to fix problem with blank and garbed in spaces in "command"
	std::stringstream intValue(numberDeleted);
	int numberInt = 0;
	intValue >> numberInt;

	try {
		// GET MESSAGE TO COMPARE 
		std::string messageOwner = getMessageOwner(numberInt);

		if(messageOwner==user){
			static_cast<void>(rediscpp::execute(*redisConnection,"del", std::to_string(numberInt))); 		// USE A "FOR AND DELETE"
			std::cout << user << " removed message: " << numberInt  << std::endl;
		}
		else{
			std::cout <<  
			user << " tried to remove message: " << 
			numberInt << " but this was write by "<< 
			messageOwner << std::endl;
		}

	}

	catch (std::exception const&)
	{
		std::cout << "message doesn't exist" << std::endl;
	}
 	clearScreen();
	printHeader();
 	sendLastMessages();
}

/* Handle all communication with the client */
void *handle_client(void *arg){
	char buff_out[BUFFER_SZ];
	char name[32];
	int leave_flag = 0;

	cli_count++;
	client_t *cli = (client_t *)arg;

	// Name
	if(recv(cli->sockfd, name, 32, 0) <= 0 || strlen(name) <  2 || strlen(name) >= 32-1){
		printf("Didn't enter the name.\n");
		leave_flag = 1;
	} else{
		strcpy(cli->name, name);
		std::cout << name << " has joined" << std::endl;
		sprintf(buff_out, "%s has joined\n", cli->name);
		
		setRedis(buff_out);
		numberUsers++;
	}

	bzero(buff_out, BUFFER_SZ);

	while(1){
		if (leave_flag) {
			break;
		}

		int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
		if (receive > 0){
			printf("%s",buff_out);									// print  buffer in this prompt
			std::string bufferIn(buff_out),
						name,
						command;
			
			// separe message to user
			size_t operator_position = bufferIn.find_first_of(":");
			name = bufferIn.substr(0, operator_position);
			command = bufferIn.substr(operator_position+2);
			// verify if the message is a delete message
			if (command.find("--rm") != std::string::npos)
			{
				remove_message(name,command);
			}
			else{
				if(strlen(buff_out) > 0){
					setRedis(buff_out); // save message in redis 
				}
			}
		} else if (receive == 0 || strcmp(buff_out, "exit") == 0){

			std::cout << name << " has left" << std::endl;
			sprintf(buff_out, "%s has left\n", cli->name);
			/////////////////////////
			setRedis(buff_out);
			numberUsers--;
			if(numberUsers <1){
				int idActual = idActualMessage-20;
				while(idActual < 0)
						idActual++;
				for(; idActual < idActualMessage; idActual++)						// CLEAR PREAVISLY MESSAGES
				{																	// FLUSHDB OR FLUSHALL
					auto const itemDel = std::to_string(idActual); 					// DOESN'T WORK IN THIS LIBRARY
					static_cast<void>(rediscpp::execute(*redisConnection,"del", itemDel)); 	// USE A "FOR AND DELETE"
				}
				idActualMessage = 0;
			}
			/////////////////////////
			leave_flag = 1;
		} else {
			printf("ERROR: -1\n");
			leave_flag = 1;
		}

		bzero(buff_out, BUFFER_SZ);
	}

  	/* Delete client from queue and yield thread */
  	close(cli->sockfd);
  	queue_remove(cli->uid);
  	free(cli);
  	cli_count--;
  	pthread_detach(pthread_self());

	return NULL;
}

int main(int argc, char **argv){
	if(argc != 2){
		printf("Usage: %s <port>\n", argv[0]);
		return EXIT_FAILURE;
	}
	const char *ip = "127.0.0.1";
	int port = atoi(argv[1]);
	int option = 1;
	int listenfd = 0, connfd = 0; 
  	struct sockaddr_in serv_addr;
  	struct sockaddr_in cli_addr;
  	pthread_t tid;

	/* Socket settings */
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(ip);
	serv_addr.sin_port = htons(port);

  	/* Ignore pipe signals */
	signal(SIGPIPE, SIG_IGN);

	if(setsockopt(listenfd, SOL_SOCKET,(SO_REUSEPORT | SO_REUSEADDR),(char*)&option,sizeof(option)) < 0){
		perror("ERROR: setsockopt failed");
    	return EXIT_FAILURE;
	}

	/* Bind */
  	if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
    	perror("ERROR: Socket binding failed");
    	return EXIT_FAILURE;
  	}

  	/* Listen */
  	if (listen(listenfd, 10) < 0) {
    	perror("ERROR: Socket listening failed");
    	return EXIT_FAILURE;
	}

	printf("=== CHAT ROOM COCUS C++ ===\n");

	while(1){
		socklen_t clilen = sizeof(cli_addr);
		connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

		/* Check if max clients is reached */
		if((cli_count + 1) == MAX_CLIENTS){
			printf("Max clients reached. Rejected: ");
			print_client_addr(cli_addr);
			printf(":%d\n", cli_addr.sin_port);
			close(connfd);
			continue;
		}

		/* Client settings */
		client_t *cli = (client_t *)malloc(sizeof(client_t));
		cli->address = cli_addr;
		cli->sockfd = connfd;
		cli->uid = uid++;

		/* Add client to the queue and fork thread */
		queue_add(cli);
		pthread_create(&tid, NULL, &handle_client, (void*)cli);

		/* Reduce CPU usage */
		sleep(1);
	}
	return EXIT_SUCCESS;
}
