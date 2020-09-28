#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <locale.h>
#include <sys/socket.h>
#include <sys/un.h>

#define N_TRAINS 5
#define N_STATIONS 8
#define N_BALISES 16
#define N_PROCESSES 6

struct path {
	int departure;
	int arrival;
	int* balises;
};

enum process_status { EXEC, STOP, TERM };

struct process {
	pid_t pid;
	enum process_status status;
};

pid_t children_group;

struct path read_train_path(int train_path_fd) {
	struct path p;
	char c;
	p.balises = malloc(N_TRAINS * sizeof(int));
	memset(p.balises, -1, N_TRAINS * sizeof(int));
	int s = 0, ma = 0, num, read_chars;
	do {
		read_chars = read(train_path_fd, &c, 1);
		if(c == 'S') {
			read_chars = read(train_path_fd, &c, 1);
			num = (int)c - 48;
			if(s == 0) {
				p.departure = num;
				s++;
			}
			else p.arrival = num;
		}
		else if(c == 'A') {
			read_chars = read(train_path_fd, &c, 1);
			num = (int)c - 48;
			p.balises[ma] = num;
			read_chars = read(train_path_fd, &c, 1);
			num = (int)c - 48;
			if(num >= 0 && num <= 9) p.balises[ma] = (p.balises[ma] * 10) + num;
			else lseek(train_path_fd, -1, SEEK_CUR);
			ma++;
		}
	} while(read_chars != 0);
	return p;
}

void create_max_files() {
	char file_path[12];
	char* w = "0";
	mkdir("MA1-16", 0777);
	int fd;
	for(int i = 1; i <= N_BALISES; i++) {
		sprintf(file_path, "MA1-16/MA%d", i);
		fd = open(file_path, O_CREAT | O_WRONLY | O_TRUNC, 0777);
		write(fd, w, sizeof(*w));
		close(fd);
	}
}

void create_log_files() {
	char file_path[12];
	mkdir("LOGS", 0777);
	int fd;
	for(int i = 1; i <= N_TRAINS; i++) {
		sprintf(file_path, "LOGS/T%d.log", i);
		fd = open(file_path, O_CREAT | O_TRUNC, 0777);
		close(fd);
	}
}

void create_rbc_log_file() {
	mkdir("LOGS", 0777);
	int fd = open("LOGS/RBC.log", O_CREAT | O_TRUNC, 0777);
	close(fd);
}

void update_rbc_log(int index, char* current, char* next, int r) {
	char strLog[300];
	char strTime[30];
	struct tm *sTm;
	int fd = open("LOGS/RBC.log", O_WRONLY | O_APPEND);
	
	time_t now = time(0);
    sTm = localtime (&now);
    strftime(strTime, sizeof(strTime), "%d %B %Y %H:%M:%S", sTm);
	sprintf(strLog, "[Treno richiedente autorizzazione: T%d], [Segmento attuale: %s], [Segmento richiesto: %s], [Autorizzato: %s], [Data: %s]\n", index, current, next, (r == 1 ? "Si" : "No"), strTime);
	write(fd, strLog, strlen(strLog) * sizeof(char));
	close(fd);
}

void update_log(int index, char* current, char* next) {
	char file_path[12];
	char strLog[100];
	char strTime[30];
	struct tm *sTm;
	int fd;
	sprintf(file_path, "LOGS/T%d.log", index);
	fd = open(file_path, O_WRONLY | O_APPEND);
	
	time_t now = time(0);
    sTm = localtime (&now);
    strftime(strTime, sizeof(strTime), "%d %B %Y %H:%M:%S", sTm);
	sprintf(strLog, "[Attuale: %s], [Next: %s], %s\n", current, next, strTime);
	write(fd, strLog, strlen(strLog) * sizeof(char));
	close(fd);
}

int toggle_balise(int balise, char w) {
	char file_path[12];
	char r;
	int fd;
	int done = 0;

	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;
	fl.l_pid = getpid();

	sprintf(file_path, "MA1-16/MA%d", balise);
	
	fd = open(file_path, O_RDWR);
	if(fcntl(fd, F_SETLK, &fl) == -1) perror("File lock");
	else {
		read(fd, &r, sizeof(char));
		if(w != r) {
			lseek(fd, -sizeof(r), SEEK_CUR);
			write(fd, &w, sizeof(char));
			done = 1;
		}

		fl.l_type = F_UNLCK;
	    if (fcntl(fd, F_SETLK, &fl) == -1) perror("File unlock");
	}

	close(fd);
	return done;
}

int check_child_exec(struct process trains[]) {
	int r = 1;
	for(int i = 1; i <= N_TRAINS; i++) if(trains[i].status != TERM) r = 0;
	return r;
}

int make_named_socket(const char *filename) {
	struct sockaddr_un socket_name;
	int sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if(sock < 0) {
		perror("Socket init");
		exit(EXIT_FAILURE);
	}
	socket_name.sun_family = AF_UNIX;
	strcpy(socket_name.sun_path, filename);
	unlink(filename);
	if(bind(sock, (struct sockaddr *) &socket_name, sizeof(socket_name)) < 0) {
		perror("Socket bind");
		exit(EXIT_FAILURE);
	}
	return sock;
}

int socket_auth(int rbc_socket, int index, char* current, char* next) {
	write(rbc_socket, &index, sizeof(int));
	
	int currentLen = strlen(current);
	write(rbc_socket, &currentLen, sizeof(currentLen));
	write(rbc_socket, current, currentLen);
	
	int nextLen = strlen(next);
	write(rbc_socket, &nextLen, sizeof(nextLen));
	write(rbc_socket, next, nextLen);

	int r;
	read(rbc_socket, &r, sizeof(int));

	close(rbc_socket);

	return r;
}

void interrupt_children(int signum) {
	kill(-children_group, SIGINT);
	exit(0);
}

int sum_stations(int stations_status[]) {
	int sum = 0;
	for(int i = 1; i <= N_STATIONS; i++) {
		sum += stations_status[i];
	}
	return sum;
}

int segment_number(char* str, char c) {
	int num;
	char* firstOccurence = strchr(str, (int)c);
	if(firstOccurence != NULL) num = atoi(firstOccurence + 1);
	else num = -1;
	return num;
}

int rbc_auth(int balises_status[], int stations_status[], char* current, char* next) {
	int r = 0;
	int numCurrent = segment_number(current, 'S'), numNext = segment_number(next, 'S');
	if(numNext != -1) {
		numCurrent = segment_number(current, 'A');
		if(numCurrent != -1 && balises_status[numCurrent] == 1) {
			balises_status[numCurrent] = 0;
			stations_status[numNext]++;
			r = 1;
		}
	}
	else {
		numNext = segment_number(next, 'A');
		if(numNext != -1 && balises_status[numNext] == 0) {
			if(numCurrent != -1) stations_status[numCurrent]--;
			else {
				numCurrent = segment_number(current, 'A');
				if(numCurrent != -1 && balises_status[numCurrent] == 1) balises_status[numCurrent] = 0;
    			else printf("Errore RBC\n");
			}
			balises_status[numNext] = 1;
    		r = 1;
		}
	}
	return r;
}

void ETCS1() {
	create_log_files();
	create_max_files();
    struct process trains[N_PROCESSES]; 
    trains[0].pid = getpid();
    int index = 0;
    char file_path[8];
    struct path p;
    int fd;
    int segment = 0;

    for(int i = 1; i <= N_TRAINS; i++) {
		if(trains[index].pid > 0) {
		    trains[i].pid = fork();
		    trains[i].status = EXEC;
		    if(trains[i].pid == 0) {
		        index = i;
		        raise(SIGSTOP);
		    }
		}
	}

	if(trains[index].pid > 0) {
		children_group = trains[1].pid;
		for(int i = 1; i <= N_TRAINS; i++) {
			setpgid(trains[i].pid, children_group);
			waitpid(trains[i].pid, NULL, WUNTRACED);
		}
		kill(-children_group, SIGCONT);
    }		    

    if(trains[index].pid == 0) {
    	char current[5];
    	char next[5];
		sprintf(file_path, "T1-5/T%d", index);
    	fd = open(file_path, O_RDONLY);
    	if(fd == -1) {
    		perror("Train path file");
    	} else {
    		p = read_train_path(fd);
    		printf("Parto: T%d, S%d\n", index, p.departure);
    		sprintf(current, "S%d", p.departure);
    		sprintf(next, "MA%d", p.balises[0]);
    		update_log(index, current, next);
    		raise(SIGSTOP);
			while(segment < 5 && p.balises[segment] != -1) {
    			int done = toggle_balise(p.balises[segment], '1');
    			if(segment != 0) {
    				sprintf(current, "MA%d", p.balises[segment - 1]);
    				sprintf(next, "MA%d", p.balises[segment]);
    				update_log(index, current, next);
    			}
    			sleep(3);
    			if(done == 1) {
    				printf("Acceduto: T%d, MA%d\n", index, p.balises[segment]);
    				segment++;
    				toggle_balise(p.balises[segment - 1], '0');
    			} else {
    				printf("Accesso negato: T%d, MA%d\n", index, p.balises[segment]);
    			}
    			raise(SIGSTOP);
    		}
    		printf("Arrivo: T%d, S%d\n", index, p.arrival);
    		sprintf(current, "MA%d", p.balises[segment - 1]);
    		sprintf(next, "S%d", p.arrival);
    		update_log(index, current, next);
    	}
    	close(fd);
    	exit(0);
    }
	
	while(check_child_exec(trains) == 0) {
		for(int i = 1; i <= N_TRAINS; i++) {
			if(trains[i].status == EXEC) {
				if(kill(trains[i].pid, 0) == -1) {
					trains[i].status = TERM;
				} else {
					waitpid(trains[i].pid, NULL, WUNTRACED);
				}
			}
		}
		printf("\n"); // Per separare gli output dei figli
		kill(-children_group, SIGCONT);
	}
}

void ETCS2() {
	create_log_files();
	create_max_files();
    struct process trains[N_PROCESSES]; 
    trains[0].pid = getpid();
    int index = 0;
    char file_path[8];
    struct path p[N_TRAINS];
    int fd;
    int segment = 0;
    
    int rbc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un socket_name;
	socket_name.sun_family = AF_UNIX;
	strcpy(socket_name.sun_path, "RBC");

	for(int i = 1; i <= N_TRAINS; i++) {
        if(trains[index].pid > 0) {
        	sprintf(file_path, "T1-5/T%d", i);
    		fd = open(file_path, O_RDONLY);
    		if(fd == -1) {
    			perror("Train path file");
    		} else {
    			p[i - 1] = read_train_path(fd);
			}
			close(fd);
        	trains[i].pid = fork();
        	trains[i].status = EXEC;
        	if(trains[i].pid == 0) {
        		index = i;
        		raise(SIGSTOP);
        	}
        }
    }

	if(trains[index].pid > 0) {
		children_group = trains[1].pid;
		for(int i = 1; i <= N_TRAINS; i++) {
			setpgid(trains[i].pid, children_group);
			waitpid(trains[i].pid, NULL, WUNTRACED);
    	}
    	if(connect(rbc_socket, (struct sockaddr *) &socket_name, sizeof(socket_name)) != -1) {
    		write(rbc_socket, p, sizeof(p)); 
	    	close(rbc_socket);
    		kill(-children_group, SIGCONT);
    	}
    }

    if(trains[index].pid == 0) {
		rbc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
		socket_name.sun_family = AF_UNIX;
		strcpy(socket_name.sun_path, "RBC");

		char current[5];
		char next[5];
		if(connect(rbc_socket, (struct sockaddr *) &socket_name, sizeof(socket_name)) != -1) {
			sprintf(current, "%s%d", "S", p[index - 1].departure);
			sprintf(next, "%s%d", "MA", p[index - 1].balises[0]);
			socket_auth(rbc_socket, index, current, next);
			toggle_balise(p[index - 1].balises[segment], '1');
			printf("Parto: T%d, MA%d\n", index, p[index -1].balises[segment]);
    		update_log(index, current, next);
    		raise(SIGSTOP);
		}
		
		while(segment < 4 && p[index - 1].balises[segment + 1] != -1) {
			sleep(3);
			rbc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
			socket_name.sun_family = AF_UNIX;
			strcpy(socket_name.sun_path, "RBC");
			if(connect(rbc_socket, (struct sockaddr *) &socket_name, sizeof(socket_name)) != -1) {
				sprintf(current, "%s%d", "MA", p[index - 1].balises[segment]);
				sprintf(next, "%s%d", "MA", p[index - 1].balises[segment + 1]);
				int r = socket_auth(rbc_socket, index, current, next);
				int done = toggle_balise(p[index - 1].balises[segment + 1], '1');
				if(r == 1) {
					toggle_balise(p[index - 1].balises[segment], '0');
					printf("Acceduto: T%d, MA%d\n", index, p[index - 1].balises[segment + 1]);
					segment++;
				} else if(r == 0) {
					if(done == 1) {
						toggle_balise(p[index - 1].balises[segment + 1], '0');
					}
					printf("Accesso negato: T%d, MA%d\n", index, p[index - 1].balises[segment + 1]);
				}
				update_log(index, current, next);
			}
			raise(SIGSTOP);
		}
		sleep(3);
		rbc_socket = socket(AF_UNIX, SOCK_STREAM, 0);
		socket_name.sun_family = AF_UNIX;
		strcpy(socket_name.sun_path, "RBC");
		if(connect(rbc_socket, (struct sockaddr *) &socket_name, sizeof(socket_name)) != -1) {
			sprintf(current, "%s%d", "MA", p[index - 1].balises[segment]);
			sprintf(next, "%s%d", "S", p[index - 1].arrival);
			socket_auth(rbc_socket, index, current, next);
			toggle_balise(p[index - 1].balises[segment], '0');
			printf("Arrivo: T%d, S%d\n", index, p[index - 1].arrival);
			update_log(index, current, next);
		}
		
    	exit(0);
    }
	
	while(check_child_exec(trains) == 0) {
		for(int i = 1; i <= N_TRAINS; i++) {
			if(trains[i].status == EXEC) {
				if(kill(trains[i].pid, 0) == -1) {
					trains[i].status = TERM;
				} else {
					waitpid(trains[i].pid, NULL, WUNTRACED);
				}
			}
		}
		printf("\n"); // Per separare gli output dei figli
		kill(-children_group, SIGCONT);
	}
	
}

void ETCS2_RBC() {
	create_rbc_log_file();
	int balises_status[] = {[0] = -1, [1 ... 16] = 0};
	int stations_status[] = {[0] = -1, [1 ... 8] = 0};
	int rbc_socket = make_named_socket("RBC");
	struct path p[N_TRAINS];
	if(listen(rbc_socket, N_TRAINS) < 0) perror("Socket listen");
	int actual_socket = accept(rbc_socket, NULL, NULL);
	if(actual_socket < 0) perror("Socket accept");
    read(actual_socket, p, sizeof(p));
    close(actual_socket);

    for(int i = 0; i < N_TRAINS; i++) stations_status[p[i].departure]++;
    
    int index = -1, currentLen, nextLen, first = 1, r = 0;
    while((sum_stations(stations_status) < N_TRAINS && first == 0) || first == 1) {
    	first = 0;
    	actual_socket = accept(rbc_socket, NULL, NULL);
    	int fd[2];
    	pipe(fd);
    	int pid = fork();
    	if(pid == 0) {
    		read(actual_socket, &index, sizeof(index));
    		read(actual_socket, &currentLen, sizeof(currentLen));
    		char current[currentLen + 1];
    		read(actual_socket, current, currentLen);
    		current[currentLen] = '\0';
    		read(actual_socket, &nextLen, sizeof(nextLen));
    		char next[nextLen + 1];
    		read(actual_socket, next, nextLen);
    		next[nextLen] = '\0';
    		
    		printf("T%d, Current: %s, Next: %s\n", index, current, next);

    		r = rbc_auth(balises_status, stations_status, current, next);
    		update_rbc_log(index, current, next, r);
    		
    		write(actual_socket, &r, sizeof(r));
    		close(actual_socket);

    		close(fd[0]);
    		write(fd[1], stations_status, sizeof(stations_status));
    		write(fd[1], balises_status, sizeof(balises_status));
    		close(fd[1]);
    		
    		exit(0);
    	} else {
    		if(waitpid(pid, NULL, 0) != -1) {
    			close(fd[1]);
    			read(fd[0], stations_status, sizeof(stations_status));
    			read(fd[0], balises_status, sizeof(balises_status));
    			close(fd[0]);
    		} else perror("RBC child process");
    	}
    }
    
	close(rbc_socket);
}

int main(int argc, char** argv) {
	setlocale(LC_ALL, "it_IT");
	signal(SIGINT, interrupt_children);
	if(argc - 1 > 0) {
		if(strcmp(argv[1], "ETCS1") == 0) ETCS1();
		else if(strcmp(argv[1], "ETCS2") == 0) {
			if(argc - 1 > 1 && strcmp(argv[2], "RBC") == 0) ETCS2_RBC();
			else ETCS2();
		}
		return 0;
	} else printf("Argument error");
}