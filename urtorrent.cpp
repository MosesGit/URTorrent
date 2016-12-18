#include <openssl/sha.h>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include "bencode.h"
#include "urlcode.h"
#include <ios>
#include <unistd.h>
#include <string.h>
#include <sstream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <random>
#include <vector>
#include <arpa/inet.h>
#include <pthread.h>
#include <functional>
#include <thread>
#include <chrono>
#include <math.h>

using namespace std;



pthread_mutex_t mutex_finish; //I have 3 critical region
pthread_mutex_t mutex_global;
pthread_mutex_t mutex_peers;
pthread_mutex_t mutex_files;

//global variables
vector<string> peers_ip;
vector<int> peers_port;
vector<int> peers_flag;
vector<long long> peers_down;
vector<long long> peers_up;

vector<int> block_list;

int global_bitfield[410];

long long downloaded, uploaded, bytes_left;

int port;
int seeder;
int verbose;
int piece_length;
int piece_num;
int info_length;
int bitfield_num;
unsigned char info_hash[SHA_DIGEST_LENGTH]; //20
void *connection_handler(void *); //to deal with multi-connected
void* socket_handler(void* lp); //to deal with the message
void *send_handler(void *); //to deal with the message
void *establish_handler(void *); //to deal with the message
char* bitfield;
int num_peers;
char peer_id[20];
vector<int> boardcast_have;
char pieces[410][20];
int finish;
string filename;
string announce, info_name;
//tracker info
int complete, downloaded2, incomplete, interval, min_interval;

string announce_ip;
int announce_port;

bool announcing;



struct Pass {
	int portno;
	int fd;
};

void error(const char *msg) {
	perror(msg);
	exit(0);
}

bool fileExists(string file_name) {
    ifstream infile(file_name);
    return infile.good();
}

bool pieceGood(int index, string p) {
	char* piece =(char*) malloc(piece_length);
	p.copy(piece, piece_length, 0);
	unsigned char info_hash2[SHA_DIGEST_LENGTH];
	int remain = info_length-((piece_num-1)*piece_length);
	
	if (index == piece_num-1) {
		unsigned char* subbuf = new unsigned char[remain];
		memcpy((char*)subbuf, piece, remain);
		//cout<<subbuf<<endl;
		SHA1(subbuf, remain, info_hash2);
		delete[] subbuf;
	}
	else {
		unsigned char* subbuf2 = new unsigned char[piece_length];
		memcpy((char*)subbuf2, piece, piece_length);
		SHA1(subbuf2, piece_length, info_hash2);
		delete[] subbuf2;
	}
	if(verbose){
		for (int i = 0; i < 20; i++)
			printf("%02x", info_hash2[i]);
		printf("\n");
		for (int j = 0; j < 20; j++)
			printf("%02hhx", pieces[index][j]);
		printf("\n");
	}
	if (memcmp(info_hash2, reinterpret_cast<unsigned char*>(pieces[index]), 20) == 0){
		free(piece);
		return true;
	}
	else {
		free(piece);
		return false;
	}
}

string readPiece(string file_name, int index) {
	int start, end;
	int length;
	int remain;
	if (index == piece_num-1) {
		remain = info_length-((piece_num-1)*piece_length);
		length = remain;
		//cout<<info_length<<endl;
		//cout<<length<<endl;
	}
	else
		length = piece_length;
	char* buffer = new char[length];
	pthread_mutex_lock(&mutex_files);
	ifstream infile(file_name, ios::binary);
	if (infile.is_open()) {
		start = index*piece_length;
		if (index == piece_num-1) {
			end = (index)*piece_length + remain;
		}
		else {
			end = (index+1)*piece_length;
		}
		infile.seekg(start);
		infile.read(buffer, end-start);
		string str(buffer, length);
		delete[] buffer;
		//cout<<buffer<<endl;
		infile.close();
		pthread_mutex_unlock(&mutex_files);
		return str;
	}
	else {
		error("ERROR reading piece from file");
	}

}

void writePiece(string file_name, int index, string p) {
    char* piece = (char*)malloc(piece_length);
    p.copy(piece, piece_length, 0);
    if (fileExists(file_name)) {
        ofstream outfile;
        int start, end;
		pthread_mutex_lock(&mutex_files);
        outfile.open(file_name, ios::binary | ios::out | ios::in);
        
        start = index*piece_length;
        int remain = info_length-((piece_num-1)*piece_length);
        if (index == piece_num-1) {
            end = (index)*piece_length + remain;
            //cout<<index<<" "<<remain<<" "<<end<<endl;
            
            outfile.seekp(start);
            outfile.write(piece, end-start);
            //cout<<"last piece"<<endl;
        }
        else {
            end = (index+1)*piece_length;
            
            outfile.seekp(start);
            outfile.write(piece, end-start);
        }
        outfile.close();
		pthread_mutex_unlock(&mutex_files);
    }
}


void createFile(string file_name, int file_size) {
	vector<char> empty(1, 0);
	ofstream outfile(file_name, ios::binary | ios::out);
	if (!outfile) {
		error("ERROR creating file");
	}
	for (int i = 0; i < file_size; i++) {
		if (!outfile.write(&empty[0], empty.size())) {
			error("ERROR writing to file");
		}
	}
	outfile.close();
}

bool checkFile(string file_name) {
	bytes_left = info_length;
	int length;
	int remain = info_length-((piece_num-1)*piece_length);
	bool is_complete = true;
	for (int i = 0; i < piece_num; i++) {
		if (i == piece_num-1) {
			length = remain;
		}
		else {
			length = piece_length;
		}
		string piece = readPiece(filename, i);
		//cout<<piece<<endl;
		
		bool check = pieceGood(i, piece);
		//cout<<check<<endl;
		if (check) {
			bytes_left -= length;
			bitfield[i] = '1';
		}
		else {
			bitfield[i] = '0';
			is_complete = false;
		}
	}
	return is_complete;
}

char* int_to_bytes( int value )   
{   
    char* src = new char [5];  
    src[0] =  (char) ((value>>24) & 0xFF);  
    src[1] =  (char) ((value>>16) & 0xFF);  
    src[2] =  (char) ((value>>8) & 0xFF);    
    src[3] =  (char) (value & 0xFF); 
    src[4] = 0;                 
    return src;   
}  

string build_message(int length, char ID, string payload) {
	//cout<<"??"<<endl;
	string message;
	char* length_char = int_to_bytes(length);
	for (int i = 0; i < 4; i++)
		message.push_back(length_char[i]);
	message += ID;
	message += payload;
	//cout<<"??"<<endl;
	//for (int i = 0; i < length; i++)
		//printf("%02x", (unsigned char)message[i]);
	return message;
}

string build_request(unsigned char* hash, string peer_id, int port, int up, int down, int left, int comp, string event) {
	char request[1024];
	strcpy(request, "GET /announce?info_hash=");
	string hash2((char*)hash);
	strcat(request, encode(hash2).c_str());
	strcat(request, "&peer_id=");
	strcat(request, encode(peer_id).c_str());
	strcat(request, "&port=");
	strcat(request, to_string(port).c_str());
	strcat(request, "&uploaded=");
	strcat(request, to_string(up).c_str());
	strcat(request, "&downloaded=");
	strcat(request, to_string(down).c_str());
	strcat(request, "&left=");
	strcat(request, to_string(left).c_str());
	strcat(request, "&compact=");
	strcat(request, to_string(comp).c_str());
	strcat(request, "&event=");
	strcat(request, event.c_str());
	strcat(request, " HTTP/1.1\r\n\r\n");
	string str(request);
	return str;
}

void print_metainfo(string file_name, string peer_id) {
	//unsigned char id_buf[8192];
	//unsigned char id_hash[20];
	//SHA1(id_buf, sizeof(id_buf) - 1, id_hash);
	printf("\tIP/port    : 127.0.0.1/%d\n", port);
	printf("\tID         : %s\n", peer_id.c_str());
	//printf("\tID         : ");
	//for (int i = 0; i < 20; i++)
	//	printf("%02x", id_hash[i]);
	//printf("\n");
	printf("\tmetainfo file : %s\n", file_name.c_str());
	printf("\tinfo hash     : ");
	for (int i = 0; i < 20; i++)
		printf("%02x", info_hash[i]);
	printf("\n");
	printf("\tfile name     : %s\n", info_name.c_str());
	printf("\tpiece length  : %d\n", piece_length);
	printf("\tfile size     : %d (%d * [piece length] + %d)\n", info_length, piece_num-1, info_length-(piece_num-1)*piece_length);
	printf("\tannounce URL  : %s\n", announce.c_str());
	printf("\tpieces' hashes:\n");
	for (int i = 0; i < piece_num; i++) {
		printf("\t%d\t", i);
		for (int j = 0; j < 20; j++)
			printf("%02hhx", pieces[i][j]);
		printf("\n");
	}
	printf("\n");
}

void print_tracker_info(int option, int comp, int down, int inc, int inter, int min) {
	printf("\t%-9s | %-10s | %-10s | %-8s | %-12s\n", "complete", "downloaded", "incomplete", "interval", "min interval");
	printf("\t---------------------------------------------------------------\n");
	printf("\t%-9d | %-10d | %-10d | %-8d | %-12d\n", comp, down, inc, inter, min);
	printf("\t---------------------------------------------------------------\n");
	int vector_size = peers_ip.size();
	if (option == 0) {
		printf("\tPeer List: \n");
		printf("\t%-16s | %-5s\n", "IP", "Port");
		printf("\t-------------------------------\n");
		for (int i = 0; i < vector_size; i++) {
			string ip_i = peers_ip[i];
			string port_i = to_string(peers_port[i]);
			printf("\t%-16s | %-5s\n", ip_i.c_str(), port_i.c_str());
		}
	}
	else {
		printf("\tPeer List (self included): \n");
		printf("\t\t%-16s | %-5s\n", "IP", "Port");
		printf("\t\t-------------------------------\n");
		for (int i = 0; i < vector_size; i++) {
			string ip_i = peers_ip[i];
			string port_i = to_string(peers_port[i]);
			printf("\t\t%-16s | %-5s\n", ip_i.c_str(), port_i.c_str());
		}
	}
}

void print_show() {
	printf("\t%-2s | %-15s | %-6s | %-10s | %-10s | %s\n", "ID", "IP address", "Status", "Down/s", "Up/s", "Bitfield");
	printf("\t---------------------------------------------------------------------\n");
	pthread_mutex_lock(&mutex_peers);
	for (int i = 0; i < peers_ip.size(); i++) {
		//cout<<peers_down[i]<<endl;
		string ip_i = peers_ip[i];
		printf("\t%-2d | %-15s | %-6s | %-10d | %-10d | %s\n", i, ip_i.c_str(), "0000", peers_down[i], peers_up[i], bitfield);
	}
	pthread_mutex_unlock(&mutex_peers);
	printf("\n");
}

//print download info
void print_status() {
	printf("\t%-10s | %-10s | %-10s | %-16s\n", "Downloaded", "Uploaded", "Left", "My bit field");
	printf("\t---------------------------------------------------------\n");
	printf("\t%-10d | %-10d | %-10d | %s\n", downloaded, uploaded, bytes_left, bitfield);
	//cout<<bitfield<<endl;
	printf("\n");
}

string tracker_announce(string peer_id, int port, unsigned char* hash, string event) {
	int sockfd;
	struct sockaddr_in serv_addr;
	struct in_addr ip_addr;
	struct hostent *server;
	char buffer[1024];
	char rec_buffer[1024];
	bzero(buffer, 1024);
	bzero(rec_buffer, 1024);
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) 
		error("ERROR opening socket");
	if (!inet_aton(announce_ip.c_str(), &ip_addr))
        error("ERROR parsing IP address");
	//cout<<announce_ip<<" "<<announce_ip.length()<<endl;
	server = gethostbyaddr((const void *)&ip_addr, sizeof(ip_addr), AF_INET);
	if (server == NULL) {
		fprintf(stderr,"ERROR, no such host\n");
		exit(0);
	}
	bzero((char *) &serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr,server->h_length);
	serv_addr.sin_port = htons(announce_port);
	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
		error("ERROR connecting3");

	//GET request
	string request;
	request = build_request(hash, peer_id, port, uploaded, downloaded, bytes_left, 1, event);
	strcpy(buffer, request.c_str());
	//cout<<"buffer "<<buffer<<endl;
	int wn = write(sockfd,buffer,strlen(buffer));//write to server
	if (wn < 0) 
		error("ERROR writing to socket");
	
	wn = read(sockfd,rec_buffer,1024);//get ACK
	//printf("%s\n",rec_buffer);
	string temp(rec_buffer, 1024);
	int start_temp = temp.find("d8:complete");
	//cout<<start_temp<<endl;
	
	//tracker response
	string response = temp.substr(0, temp.find("\r\n"));
	
	//get content length
	int len;
	char* ch = strstr(const_cast<char*>(temp.c_str()), "Content-Length:");
	int i = 0;
	char cont_len[20];
	for (i = 0; i < 20; i++) {
		char next = ch[strlen("Content-Length:")+i];
		if (next == '\r' || next == '\n') {
			break;
		}
		cont_len[i] = next;
	}
	len = atoi(cont_len);
	
	string st = temp.substr(start_temp, len);
	be_node *peer_node = be_decode(st.c_str(), len);
	start_temp = st.find("5:peers");
	int end_temp = st.find(":", start_temp+7);

	string find_peers = st.substr(start_temp+7, end_temp-7-start_temp);
	num_peers = atoi(find_peers.c_str())/6;
	
	for (int i = 0; peer_node->val.d[i].val; i++) {
		if (strcmp(peer_node->val.d[i].key,"complete")==0) {
			complete = ((peer_node->val.d[i].val)->val.i);
		}
		else if(strcmp(peer_node->val.d[i].key,"downloaded")==0) {
			downloaded2 = ((peer_node->val.d[i].val)->val.i);
		}
		else if(strcmp(peer_node->val.d[i].key,"incomplete")==0) {
			incomplete = ((peer_node->val.d[i].val)->val.i);
		}
		else if(strcmp(peer_node->val.d[i].key,"interval")==0) {
			interval = ((peer_node->val.d[i].val)->val.i);
		}
		else if(strcmp(peer_node->val.d[i].key,"min interval")==0) {
			min_interval = ((peer_node->val.d[i].val)->val.i);
		}
		else if(strcmp(peer_node->val.d[i].key,"peers")==0) {
			for (int j = 0; j < num_peers; j++) {
				if (verbose) {
					cout<<"ss "<<(int)(*((peer_node->val.d[i].val)->val.s+(j*6)))<<endl;
					cout<<"ss "<<(int)(*((peer_node->val.d[i].val)->val.s+(j*6)+1))<<endl;
					cout<<"ss "<<(int)(*((peer_node->val.d[i].val)->val.s+(j*6)+2))<<endl;
					cout<<"ss "<<(int)(*((peer_node->val.d[i].val)->val.s+(j*6)+3))<<endl;
					cout<<"ss "<<((unsigned char)(*((peer_node->val.d[i].val)->val.s+(j*6)+4)))*256+(unsigned char)(*((peer_node->val.d[i].val)->val.s+(j*6)+5))<<endl;
					cout<<"ss "<<endl;
				}
				stringstream strStream;
				
				strStream<<(int)(unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6));
				strStream<<".";
				strStream<<(int)(unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6)+1);
				strStream<<".";
				strStream<<(int)(unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6)+2);
				strStream<<".";
				strStream<<(int)(unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6)+3);
				string addr_temp = strStream.str();
				//cout<<"addr_temp "<<addr_temp<<endl;
				int temp_port = ((unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6)+4))*256 + (unsigned char)*((peer_node->val.d[i].val)->val.s+(j*6)+5);
				
				bool has = false;
				pthread_mutex_lock(&mutex_peers);
				if (!peers_ip.empty()) {
					int vector_size = peers_ip.size();
					for (int k = 0; k < vector_size; k++) {
						string ip_k = peers_ip[k];
						int port_k = peers_port[k];
						if (ip_k == addr_temp && port_k == temp_port) {
							has = true;
						}
					}
					if (!has) {
						int check_blocked = 1;
						for(int m=0; m<block_list.size(); m++){
							if(temp_port==block_list[m])
								check_blocked = 0;
						}

						if(memcmp(addr_temp.c_str(), "0.0.0.0", 7)!=0 && check_blocked){
							peers_ip.push_back(addr_temp);
							peers_port.push_back(temp_port);
							peers_flag.push_back(0);
							peers_down.push_back(0);
							peers_up.push_back(0);
						}
					}
				}
				else {
					int check_blocked = 1;
						for(int m=0; m<block_list.size(); m++){
							if(temp_port==block_list[m])
								check_blocked = 0;
						}
					if(memcmp(addr_temp.c_str(), "0.0.0.0", 7)!=0){
						peers_ip.push_back(addr_temp);
						peers_port.push_back(temp_port);
						peers_flag.push_back(0);
						peers_down.push_back(0);
						peers_up.push_back(0);
					}
				}
				pthread_mutex_unlock(&mutex_peers);
			}
		}
		//cout<<"str: "<<(n->val.d[i].key)<<endl;
	}
	
	if (peer_node)
		be_free(peer_node);
	
	close(sockfd);
	return response;
}

void periodic_announce(string peer_id, int port, unsigned char* hash1) {
	while (1) {
		//realistically n should be between min_interval and interval
		//but for our purposes we will go with a smaller number
		int n;
		string response;
		//n = min_interval;
		n = 10;
		tracker_announce(peer_id, port, hash1, "");
		this_thread::sleep_for(chrono::seconds(n));
	}
}

void start_announce_thread(string peer_id, int port, unsigned char* hash1) {
	thread t(periodic_announce, peer_id, port, hash1);
	t.detach();
	this_thread::sleep_for(chrono::seconds(1));
	//cout<<"seeder "<<seeder<<endl;
	
}

int main(int argc, char* argv[]) {
	announcing = false;
	strcpy(peer_id, "UR-1-0--");
	//random numbers
	srand(time(NULL));
	for (int i = 0; i < 12; i++) {
		strcat(peer_id, to_string(rand() % 10).c_str());
	}
	
	if (argc < 3) {
		fprintf(stderr,"need filename and port number\n");
		exit(0);
	}
	port = atoi(argv[2]);
	string torrent_name = argv[1];
	ifstream infile(torrent_name);
	if(argc == 4)
		verbose = atoi(argv[3]);
	else
		verbose = 0;
	
	char buffer[1024];
	bzero(buffer, 1024);
	char rec_buffer[1024];
	bzero(rec_buffer, 1024);
	
	if (!infile) {
		fprintf(stderr, "file open error\n");
		exit(0);
	}
	//cout<<"until now"<<endl;
	pthread_t tid;
	if (pthread_create( &tid, NULL, connection_handler, (void*) 0) < 0) {
		perror("Error on create thread");
	}
	
	if(verbose)
		cout<<"torrent_name "<<torrent_name<<endl;
	
	string buf;
	infile>>noskipws;  
	long long length = 0;
	while (!infile.eof()) {
		char pr;
		infile>>pr;
		buf = buf + pr;
		length++;
	}
	
	//cout<<buf<<endl;
	int info_start = buf.find("4:info");
	//string subbuf = buf.substr(info_start+6, length-8-info_start);
	unsigned char subbuf[8192];
	memcpy((char*)subbuf, (buf.substr(info_start+6, length-8-info_start)).c_str(), length-8-info_start);
	SHA1(subbuf, length-8-info_start, info_hash);
	//cout<<"info_start "<<info_start<<endl;
	//printf("info_hash: ");
	//for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
	//	printf("%02x", info_hash[i]);
	//printf("\n");
	info_start = buf.find("6:pieces");
	int info_end = buf.find(":", info_start+8);
	piece_num = atoi((buf.substr(info_start+8, info_end-8-info_start)).c_str())/20;
	bitfield_num = piece_num/8 + 1;
	finish = piece_num;
	//cout<<"piece_num "<<piece_num<<endl;
	infile.close();
	be_node *n = be_decode(buf.c_str(), length);

	string hash2((char*)info_hash);
	for (int i = 0; n->val.d[i].val; i++) {
		if (strcmp(n->val.d[i].key,"announce")==0) {
			announce = ((n->val.d[i].val)->val.s);
		}
		else if (strcmp(n->val.d[i].key,"info")==0) {
			for (int j = 0; (n->val.d[i].val)->val.d[j].val; j++) {
				//cout<<"info: "<<((n->val.d[i].val)->val.d[j].key)<<endl;
				if (strcmp((n->val.d[i].val)->val.d[j].key,"length")==0) {
					info_length = ((n->val.d[i].val)->val.d[j].val)->val.i;
				}
				else if (strcmp((n->val.d[i].val)->val.d[j].key,"name")==0) {
					info_name = ((n->val.d[i].val)->val.d[j].val)->val.s;
				}
				else if (strcmp((n->val.d[i].val)->val.d[j].key,"piece length")==0) {
					piece_length = ((n->val.d[i].val)->val.d[j].val)->val.i;
				}
				else if (strcmp((n->val.d[i].val)->val.d[j].key,"pieces")==0) {
					for (int k = 0; k < piece_num; k++) {
						memcpy(pieces[k], ((n->val.d[i].val)->val.d[j].val)->val.s+20*k, 20);
						//printf("%d	", k);
						//for (int l = 0; l < 20; l++)
						//	printf("%02hhx", pieces[k][l]);
						//printf("\n");
					}
				}
			}
		}
	}
	int announce_start, announce_mid, announce_end;
	//announce_start = announce.find("http://") + 7;
	announce_start = 7;
	announce_mid = announce.rfind(":");
	announce_end = announce.find("/announce");
	announce_ip = announce.substr(announce_start, announce_mid-announce_start);
	announce_port = atoi(announce.substr(announce_mid+1, announce_end-(announce_mid+1)).c_str());
	//cout<<"announce "<<announce_ip<<" "<<announce_port<<endl;
	downloaded = 0;
	uploaded = 0;
	filename = torrent_name.substr(0, torrent_name.rfind("."));
	bitfield = (char*) malloc(piece_num+1);
	memset(bitfield, '0', piece_num);
	bitfield[piece_num] = '\0';
	//cout<<"test "<<piece_num<<" "<<bitfield<<endl;
	if (fileExists(filename)) {
		if (checkFile(filename)) {
			seeder = 1;
			bytes_left = 0;
		}
		else {
			seeder = 0;
			start_announce_thread(peer_id, port, info_hash);
			announcing = true;
		}
	}
	else {
		seeder = 0;
		memset(bitfield, '0', piece_num);
		bytes_left = info_length;
		cout<<"Original file not found, creating "<<filename<<"..."<<endl;
		createFile(filename, info_length);
		start_announce_thread(peer_id, port, info_hash);
		announcing = true;
	}
	
	//cout<<"bitfield "<<bitfield<<endl;
	
	while(1) {
		cout<<"urtorrent>";
		string command;
		cin>>command;
		if (command == "metainfo") {
            print_metainfo(torrent_name, peer_id);
		}
		else if (command == "announce") {
			string response;
			response = tracker_announce(peer_id, port, info_hash, "started");
			cout<<"\tTracker responded: "<<response<<endl;
			print_tracker_info(0, complete, downloaded, incomplete, interval, min_interval);
			if (!announcing) {
				start_announce_thread(peer_id, port, info_hash);
				announcing = true;
			}
			if (!seeder) {
		pthread_t tid;
		if (pthread_create(&tid, NULL, establish_handler, (void*) 0) < 0) {
			perror("Error on create thread");
		}
	}
		}
		else if (command == "trackerinfo") {
			print_tracker_info(1, complete, downloaded, incomplete, interval, min_interval);
		}
		else if (command == "show") {
			print_show();
		}
		else if (command == "status") {
			
			print_status();
		}
		else if (command == "quit") {
			if (n)
				be_free(n);
			return 0;
		}
	}
}

void *connection_handler(void *) {
	while (1) {
		int sockfd;
		socklen_t clilen;
		int *newsockfd;
  		char *deal_sock;
		struct sockaddr_in serv_addr, cli_addr;  
		//printf("Waiting for data from sender \n");  
		
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) 
			error("ERROR opening socket");
		bzero((char *) &serv_addr, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = INADDR_ANY;
		serv_addr.sin_port = htons(port);
		
		int on = 1;  
		if ((setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on)))<0)  
			error("setsockopt failed");  
		if (bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) 
			error("ERROR on binding");
		listen(sockfd,10);
		clilen = sizeof(cli_addr);
		while(1){
			pthread_t thread_id;
			newsockfd = (int*)malloc(sizeof(int));
			*newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
			if(*newsockfd!=-1){
				
          		pthread_create(&thread_id,0,&socket_handler, (void*)newsockfd );
            	//pthread_detach(thread_id);
			}
			
		}
		close(sockfd);
	}
}
			/*deal_sock = (char*)malloc(piece_length);
				memcpy(deal_sock, message, piece_length); //prepare the thread  
			delete [] message;
			pthread_t tid;

			if( pthread_create( &tid, NULL,  message_handler, (void*) deal_sock) < 0)
				perror("Error on create thread");*/
void* socket_handler(void* lp){
    int *newsockfd = (int*)lp;
    int on = 1; 
    int connection_port=0;
	int flag_port=1;
    char * message = new char [piece_length+13]; 
    if(verbose)
   		cout<<"piece_length "<<piece_length<<endl;
    while(1){
    	if(verbose)
    		cout<<"new read "<<connection_port<<endl;
		on = read(*newsockfd,message,piece_length+13);
		
		if (on < 0)
			error("ERROR reading from socket");
		if (memcmp(message+1, "URTorrent protocol", 18)==0) {//get handshake
			if(verbose)
				printf("Handshake from server:%s\n",message);
			int rec_length = (int)*(unsigned char*)message;
			if(flag_port){
				connection_port = (unsigned char)*(message+47)*256 + (unsigned char)*(message+48);
				flag_port = 0;
			}
			char check_hash[SHA_DIGEST_LENGTH];
			if(verbose)
				cout<<"rec_length "<<rec_length<<endl;
			memcpy(check_hash, (message+27), SHA_DIGEST_LENGTH);
			char check_id[20];
			memcpy(check_id, (message+47), 20);
			if(verbose){
				printf("info_hash: ");
				for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
					printf("%02x", info_hash[i]);
				printf("\n");
				printf("check_hash: ");
				for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
					printf("%02x", (unsigned char)check_hash[i]);
				printf("\n");
			}
			if(memcmp(check_hash, (char*)info_hash, SHA_DIGEST_LENGTH)==0){
				string reply_handshake;
				int reply_handshake_length = 4+1+bitfield_num;
				for (int j = 0; j < piece_num; j+=8) {
					char to_byte = (char) 0;
					for(int k=0; k<8; k++){
						if(*(bitfield+j+k)=='1'){
							to_byte += (char)pow(2,7-k);
						}
					}
					
					reply_handshake.push_back(to_byte);
				}
				printf("reply_handshake %d %d\n", bitfield_num, piece_num);
				for (int j = 0; j < bitfield_num; j++)
					printf("%02x", (unsigned char)reply_handshake[j]);
				printf("\n");
				//while(1);
				//build_message(reply_handshake_length, (char)5, reply_handshake);
				on = write(*newsockfd,build_message(reply_handshake_length, (char)5, reply_handshake).c_str(),reply_handshake_length);
				if (on < 0)
					error("ERROR writing from socket");

			}
			else
				error("info_hash is not match");
		}
		else if (*(message+4) == (char)6) { //get request
			//time upload start
			double up_start, up_end;
			up_start = chrono::duration_cast<chrono::milliseconds >(chrono::system_clock::now().time_since_epoch()).count();
			
			if (verbose)
				printf("id 6\n");
			if (verbose) {
				for (int j = 0; j < 17; j++)
					printf("%02x", (unsigned char)message[j]);
				printf("\n");
			}
			int piece_index = (unsigned char)message[5]*16777216 + (unsigned char)message[6]*65536 + (unsigned char)message[7]*256 + (unsigned char)message[8];
			if (verbose)
				cout<<"piece_index "<<piece_index<<endl;
			char* piece_buffer = (char*) malloc(piece_length+13);
			piece_buffer[4] = (char) 7;
			string piece_send;
			if (piece_index<piece_num) {
				char* length_char = int_to_bytes(piece_length+9);
				piece_buffer[0] = length_char[0];
				piece_buffer[1] = length_char[1];
				piece_buffer[2] = length_char[2];
				piece_buffer[3] = length_char[3];
				delete [] length_char;
				piece_buffer[5] = message[5];
				piece_buffer[6] = message[6];
				piece_buffer[7] = message[7];
				piece_buffer[8] = message[8];
				piece_buffer[9] = (char)0;
				piece_buffer[10] = (char)0;
				piece_buffer[11] = (char)0;
				piece_buffer[12] = (char)0;
				piece_send = readPiece(filename, piece_index);
				//cout<<"piece_send.c_str() "<<piece_send.c_str()<<endl;
				memcpy(piece_buffer+13, piece_send.c_str(), piece_length);
			}
			else {
				piece_buffer[0] = (char)0;
				piece_buffer[1] = (char)0;
				piece_buffer[2] = (char)0;
				piece_buffer[3] = (char)9;
			}
			on = write(*newsockfd, piece_buffer, piece_length+13);
			int remain = info_length-((piece_num-1)*piece_length); 
			if (piece_index == piece_num-1)
				uploaded += remain;
			else
				uploaded += piece_length;
			free(piece_buffer);
			
			up_end = chrono::duration_cast<chrono::milliseconds >(chrono::system_clock::now().time_since_epoch()).count();
			//time upload end
			
			double dur = (up_end-up_start);
			if (dur < 1)
				dur = 1;
			//cout<<"dur "<<dur<<endl;
			long long up_rate = (long long) 1000*((double)piece_length/dur);
			//cout<<"should be "<<up_rate<<endl;
			for (int i = 0; i < peers_ip.size(); i++) {
				//cout<<"cmp ports "<<connection_port<<" "<<peers_port[i]<<endl;
				if (connection_port == peers_port[i]) {
					peers_up[i] = up_rate;
					//cout<<"upload test"<<endl;
				}
			}
			//cout<<"up "<<up_rate<<endl;
		}
		else if(*(message+4) == (char)4){//get have
			if(verbose)
				printf("id 4\n");
			if (verbose) {
				for (int j = 0; j < 13; j++)
					printf("%02x", (unsigned char)message[j]);
				printf("\n");
			}
			int have_index = (unsigned char)message[5]*16777216 + (unsigned char)message[6]*65536 + (unsigned char)message[7]*256 + (unsigned char)message[8];
			int sender_port = (unsigned char)message[9]*256 + (unsigned char)message[10];
			if(verbose)
				cout<<"have_index "<<have_index<<" "<<sender_port<<endl;
			pthread_mutex_lock(&mutex_global);
			global_bitfield[have_index] = sender_port;
			//cout<<"GGGlo "<<have_index<<" "<<sender_port<<endl;
			pthread_mutex_unlock(&mutex_global);
		}
		else
			cout<<"wtf"<<endl;
	}
FINISH:
    free(newsockfd);
    return 0;
}

void *send_handler(void *arg) {
	Pass *peer_pass = (Pass*)arg;
	char handshake_buffer[67];
					
	handshake_buffer[0]=(char)67;
					//int rec_length = (int)*handshake_buffer;
					
					//cout<<"rec_length "<<rec_length<<" "<<(int)handshake_length<<endl;
	memcpy(handshake_buffer+1, "URTorrent protocol",18);
					//printf("handshake_buffer: %s\n", handshake_buffer);
	for(int k=0; k<8; k++)
		handshake_buffer[19+k]=(char)0;
					
	memcpy((char*)(handshake_buffer+27), (char*)info_hash, SHA_DIGEST_LENGTH);
					//strcat(handshake_buffer, info_hash_temp);

	memcpy((char*)(handshake_buffer+47), peer_id, 20);
	char* get_port = int_to_bytes(port);
	memcpy((char*)(handshake_buffer+47), get_port+2, 1);
	memcpy((char*)(handshake_buffer+48), get_port+3, 1);
	delete [] get_port;
	if (verbose) {
		printf("handshake_buffer: ");
		for (int i = 0; i < 67; i++)
			printf("%02x", (unsigned char)handshake_buffer[i]);
		printf("\n");
	}
	int sn = write(peer_pass->fd,handshake_buffer, 67);//send handshake
	char* bitfield_buffer = (char*)malloc(5+piece_num);
	sn = read(peer_pass->fd,bitfield_buffer, 5+piece_num);
	if(verbose){
		printf("bitfield from server:%s");
		for(int i=0; i<piece_num; i++)
			printf("%02x", bitfield_buffer[i]);
		printf("\n");
	}
	vector<char> peer_bitfield;
	for(int i=0; i<piece_num; i++){
		if(*(bitfield_buffer+5+i)=='1')
			peer_bitfield.push_back('1');
		else
			peer_bitfield.push_back('0');
	}
	free(bitfield_buffer);
	
	double down_start, down_end;
	
	while (finish > 0) {
		//time download start
		down_start = chrono::duration_cast<chrono::milliseconds >(chrono::system_clock::now().time_since_epoch()).count();
		
		/*if(!boardcast_have.empty()){
			char *have_buffer = (char*)malloc(9);
		}*/
		pthread_mutex_lock(&mutex_finish);
		//cout<<"finish "<<finish<<endl;
		//cout<<"6 "<<bitfield<<endl;
		//cout<<"ppport "<<peer_pass->portno<<endl;
		//printf("6 ");
		//for(int i=0; i<piece_num; i++)
		//	printf("%c",peer_bitfield[i]);
		//printf("\n");
		int i;
		pthread_mutex_lock(&mutex_global);
		for (i = rand()%piece_num; i < piece_num; i++) {
			if (*(bitfield+i)=='0') {
				if (peer_bitfield[i]=='1'||(global_bitfield[i]==peer_pass->portno))
					break;
			}
		}
		pthread_mutex_unlock(&mutex_global);
		//cout<<"iiiiiiiiiii "<<i<<endl;
		char* temp_index = int_to_bytes(i);
		/*printf("temp_index\n");
		for (int j = 0; j < 4; j++)
			printf("%02x", (unsigned char)temp_index[j]);*/
		string request_buffer;
		for(int j=0; j<4; j++){
			request_buffer.push_back( *(temp_index+j));
		}
		temp_index = int_to_bytes(0);
		for(int j=0; j<4; j++){
			request_buffer.push_back( *(temp_index+j));
		}
		temp_index = int_to_bytes(piece_length);
		for(int j=0; j<4; j++){
			request_buffer.push_back( *(temp_index+j));
		}
		delete[] temp_index;
		if(verbose){
			printf("request_temp\n");
			for (int j = 0; j < 12; j++)
				printf("%02x", (unsigned char)request_buffer[j]);
			printf("\n");
		}
		sn = write(peer_pass->fd, build_message(13, (char)6, request_buffer).c_str(), 17);//send request
		char* get_piece = (char*)malloc(piece_length+13);
		
		sn = read(peer_pass->fd, get_piece, piece_length+13);//read piece
		
		if (verbose) {
			/*printf("send get_piece\n");
			for (int j = 0; j < piece_length+13; j++)
				printf("%02x", (unsigned char)get_piece[j]);
			printf("\n");*/
		}
		int receive_index = (unsigned char)get_piece[5]*16777216 + (unsigned char)get_piece[6]*65536 + (unsigned char)get_piece[7]*256 + (unsigned char)get_piece[8];
		
		if (verbose)
			cout<<"receive_index "<<receive_index<<endl;
		if (receive_index<piece_num && receive_index>-1)
			if (bitfield[receive_index]!='1') {
				string check_get;
				for(int j=0; j<piece_length; j++){
					check_get.push_back(*(get_piece+13+j));
				}
				bool check_piece = pieceGood(receive_index, check_get);
				if(verbose)
					cout<<"check_piece "<<check_piece<<" "<<receive_index<<endl;
				if (check_piece) {
					writePiece(filename, receive_index, check_get);
					int remain = info_length-((piece_num-1)*piece_length); 
					if (receive_index == piece_num-1) {
						bytes_left -= remain;
						downloaded += remain;
					}
					else {
						bytes_left -= piece_length;
						downloaded += piece_length;
					}
					bitfield[receive_index]='1';
					//cout<<"!!!!!!!!!!!!!!!!!!!!!!!!!!! "<<bitfield<<endl;
					
						
							if(peers_port[i]!=port){
								int  notify_sockfd;
								struct hostent *hptr;
								struct sockaddr_in serv_addr, peer_addr;
							//string raddr = *(peers_ip[i])+"."+*(peers_ip[i]+1)+"."+*(peers_ip[i]+2)+"."+*(peers_ip[i]+3);
								int i=rand()%peers_ip.size();
								notify_sockfd = socket(AF_INET, SOCK_STREAM, 0);
								if (notify_sockfd < 0) 
									error("ERROR opening socket");
								bzero((char *) &serv_addr, sizeof(serv_addr));
								pthread_mutex_lock(&mutex_peers);
								if(!inet_aton(peers_ip[i].c_str(),&serv_addr.sin_addr)){
									printf("Inet_aton error\n");
									exit(1);
								}
								pthread_mutex_unlock(&mutex_peers);
								if((hptr=gethostbyaddr((void *)&serv_addr.sin_addr,4,AF_INET))==NULL){
									printf("gethostbyaddr error for addr:%s\n",peers_ip[i]);
									printf("h_errno %d\n",h_errno);
									exit(1);
								}
							
								peer_addr.sin_family = AF_INET;
								bcopy((char *)hptr->h_addr, (char *)&peer_addr.sin_addr.s_addr,hptr->h_length);
								peer_addr.sin_port = htons(peers_port[i]);
							
								if (connect(notify_sockfd,(struct sockaddr *) &peer_addr,sizeof(peer_addr)) < 0) 
									error("ERROR connecting1");
								char have_buffer[11];
								have_buffer[0] = (char)0;
								have_buffer[1] = (char)0;
								have_buffer[2] = (char)0;
								have_buffer[3] = (char)11;
								have_buffer[4] = (char)4;
								char* have_temp = int_to_bytes(receive_index);
								have_buffer[5] = have_temp[0];
								have_buffer[6] = have_temp[1];
								have_buffer[7] = have_temp[2];
								have_buffer[8] = have_temp[3];
								have_temp = int_to_bytes(port);
								have_buffer[9] = have_temp[2];
								have_buffer[10] = have_temp[3];
								delete [] have_temp;
								int hn = write(notify_sockfd, have_buffer, 11);//send have
								//close(notify_sockfd);

							}
						
					finish--;
					//cout<<"!!!!!!!!!!!!!!!!!!!!!finish "<<finish<<endl;
				}
			}
		free(get_piece);
		
		//boardcast_have.push_back(piece_index);
		
		pthread_mutex_unlock(&mutex_finish);
		
		down_end = chrono::duration_cast<chrono::milliseconds >(chrono::system_clock::now().time_since_epoch()).count();
		//time download end
		
		double dur = (down_end-down_start);
		if (dur < 1)
			dur = 1;
		long long down_rate = (long long) 1000*((double)piece_length/dur);

		pthread_mutex_lock(&mutex_peers);
		for (int i = 0; i < peers_down.size(); i++) {
			if (peer_pass->portno == peers_port[i]) {
				peers_down[i] = down_rate;
			}
		}
		pthread_mutex_unlock(&mutex_peers);
		//cout<<"down "<<down_rate<<endl;
	}
}

void *establish_handler(void *){
	while(!piece_length);
	while(finish){
		pthread_mutex_lock(&mutex_finish);
		for(int i=0; i<peers_ip.size(); i++){
			if(peers_port[i]!=port && peers_flag[i]==0){
				if(verbose){
					cout<<"peer_port[i] "<<peers_port[i]<<endl;
					cout<<"peer_ip[i] "<<peers_ip[i]<<endl;
				}
				//int * peer_sockfd = (int*)malloc(sizeof(int));
				Pass* pass_message = (Pass*)malloc(sizeof(Pass));
				pass_message->portno = peers_port[i];
			    struct hostent *hptr;
			   	pthread_t thread_id;
			    struct sockaddr_in serv_addr, peer_addr;
			    	 	//string raddr = *(peers_ip[i])+"."+*(peers_ip[i]+1)+"."+*(peers_ip[i]+2)+"."+*(peers_ip[i]+3);
			    pass_message->fd = socket(AF_INET, SOCK_STREAM, 0);
				if (pass_message->fd < 0) 
					error("ERROR opening socket");
				bzero((char *) &serv_addr, sizeof(serv_addr));
				if(!inet_aton(peers_ip[i].c_str(),&serv_addr.sin_addr)){
					printf("Inet_aton error\n");
					exit(1);
				}
				if((hptr=gethostbyaddr((void *)&serv_addr.sin_addr,4,AF_INET))==NULL){
					printf("gethostbyaddr error for addr:%s\n",peers_ip[i]);
					printf("h_errno %d\n",h_errno);
					exit(1);
				}
						
				peer_addr.sin_family = AF_INET;
				bcopy((char *)hptr->h_addr, (char *)&peer_addr.sin_addr.s_addr,hptr->h_length);
				peer_addr.sin_port = htons(peers_port[i]);
				pthread_mutex_lock(&mutex_peers);		
				if (connect(pass_message->fd,(struct sockaddr *) &peer_addr,sizeof(peer_addr)) < 0){
					//error("ERROR connecting2");
					block_list.push_back(peers_port[i]);
					vector <int>::iterator Iter;
					peers_port.erase(peers_port.begin()+i);

					peers_ip.erase(peers_ip.begin()+i);

					peers_flag.erase(peers_flag.begin()+i);
					
					peers_down.erase(peers_down.begin()+i);

					peers_up.erase(peers_up.begin()+i);
							
							
					close(pass_message->fd);
				}
				else{
					if(pass_message->fd!=-1){
					
	          		pthread_create(&thread_id,0,&send_handler, (void*)pass_message );
	            	
					}
					peers_flag[i]=1;
				}
				pthread_mutex_unlock(&mutex_peers);
			}
		}
		pthread_mutex_unlock(&mutex_finish);
	}
	//cout<<"hhhhhhhhhhhhhhhhhhhhhhhhhhh "<<bitfield<<endl;
}
