void str_overwrite_stdout() 
void str_trim_lf (char* arr, int length) 
void print_client_addr(struct sockaddr_in addr)
void queue_add(client_t *cl)
void queue_remove(int uid)
void broadcastMessage(const char* message)
void sendLastMessages(void)
void printHeader(void)
void clearScreen(void)
void setRedis(char * message)
std::string getMessageOwner(int idMessage)
void remove_message(std::string user,std::string command)
void *handle_client(void *arg)
int main(int argc, char **argv)