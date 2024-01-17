#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

// Аргумент для треда работающего с сервером
typedef struct {
	int elements_count; // Пределы
	long long *elements; // Количество попыток для сервера
	struct sockaddr_in
		*server; // Структура с информацией для подключения к серверу
	long long *results; // Куда записать результат
} thread_args_t;

// Функция треда работающего с сервером
void *send_thread(void *arg)
{
	thread_args_t *task_data = (thread_args_t *)arg;
	int servsock = socket(PF_INET, SOCK_STREAM, 0);
	if (servsock < 0) {
		perror("Create new socket to server");
		exit(EXIT_FAILURE);
	}
	struct sockaddr_in listenaddr;
	listenaddr.sin_family = AF_INET;
	listenaddr.sin_addr.s_addr = INADDR_ANY;
	listenaddr.sin_port = 0;

	if (bind(servsock, (struct sockaddr *)&listenaddr, sizeof(listenaddr)) <
	    0) {
		perror("Can't create listen socket");
		exit(EXIT_FAILURE);
	}
	socklen_t servaddrlen = sizeof(struct sockaddr_in);
	if (connect(servsock, (struct sockaddr *)task_data->server,
		    servaddrlen) < 0) {
		perror("Connect to server failed!");
		exit(EXIT_FAILURE);
	}

    int ok = 1;
	if (send(servsock, &task_data->elements_count,
		 sizeof(task_data->elements_count), 0) < 0) {
		ok = 0;
	}
	long long left_to_send =
		sizeof(long long) * task_data->elements_count;
	char *send_ptr = (char *)task_data->elements;
	while (left_to_send) {
		int sent = send(servsock, send_ptr, left_to_send, 0);
		if (sent <= 0) {
			ok = 0;
            break;
		}
		send_ptr += sent;
		left_to_send -= sent;
	}
    if(!ok || left_to_send != 0) {
        perror("Sending data to server failed");
		exit(EXIT_FAILURE);
    }

	int recv_byte =
		recv(servsock, task_data->results, sizeof(long double), 0);
	if (recv_byte == 0) {
		fprintf(stderr,
			"Server %s on port %d die!\nCancel calculate, on all",
			inet_ntoa(task_data->server->sin_addr),
			ntohs(task_data->server->sin_port));
		exit(EXIT_FAILURE);
	}
	fprintf(stdout, "Server %s on port %d finish!\n",
		inet_ntoa(task_data->server->sin_addr),
		ntohs(task_data->server->sin_port));
	return NULL;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s filename [maxserv]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int maxservu = 1000000;
	if (argc == 3) {
		maxservu = atoi(argv[2]);
		if (maxservu < 1) {
			fprintf(stderr, "Error number of max servers\n");
			exit(EXIT_FAILURE);
		}
	}
	char *filename = argv[1];
	int filed = open(filename, O_RDONLY);
	if (filed == -1) {
		perror("Can not open file!");
		exit(EXIT_FAILURE);
	}
	long long *digits = (long long *)malloc(sizeof(long long));
	int array_size = 1;
	FILE *file = fdopen(filed, "r");
	long long digit = 0;
	fscanf(file, "%lld", &digit);
	while (!feof(file)) {
		digits = realloc(digits, sizeof(long long) * array_size);
        digits[array_size - 1] = digit;
		array_size++;
		fscanf(file, "%lld", &digit);
	}
	fclose(file);
	close(filed);
	if (--array_size < 2) {
		if (array_size) {
			fprintf(stdout, "Max = %lld\n", digits[0]);
		} else {
			fprintf(stdout, "File is empty\n");
		}
		free(digits);
		exit(EXIT_SUCCESS);
	}
	// Создаем сокет для работы с broadcast
	int sockbrcast = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockbrcast == -1) {
		perror("Create broadcast socket failed");
		exit(EXIT_FAILURE);
	}

	// Создаем структуру для приема ответов на broadcast
	int port_rcv = 0;
	struct sockaddr_in addrbrcast_rcv;
	bzero(&addrbrcast_rcv, sizeof(addrbrcast_rcv));
	addrbrcast_rcv.sin_family = AF_INET;
	addrbrcast_rcv.sin_addr.s_addr = htonl(INADDR_ANY);
	addrbrcast_rcv.sin_port = 0; //htons(port_rcv);
	// Биндим её
	if (bind(sockbrcast, (struct sockaddr *)&addrbrcast_rcv,
		 sizeof(addrbrcast_rcv)) < 0) {
		perror("Bind broadcast socket failed");
		close(sockbrcast);
		exit(EXIT_FAILURE);
	}

	// Структура для отправки broadcast
	int port_snd = 38199;
	struct sockaddr_in addrbrcast_snd;
	bzero(&addrbrcast_snd, sizeof(addrbrcast_snd));
	addrbrcast_snd.sin_family = AF_INET;
	addrbrcast_snd.sin_port = htons(port_snd);
	addrbrcast_snd.sin_addr.s_addr = htonl(0xffffffff);

	// Разрешаем broadcast на сокете
	int access = 1;
	if (setsockopt(sockbrcast, SOL_SOCKET, SO_BROADCAST,
		       (const void *)&access, sizeof(access)) < 0) {
		perror("Can't accept broadcast option at socket to send");
		close(sockbrcast);
		exit(EXIT_FAILURE);
	}
	int msgsize = sizeof(char) * 18;
	void *hellomesg = malloc(msgsize);
	bzero(hellomesg, msgsize);
	strcpy(hellomesg, "Hello Server");
	// Посылаем broadcast
	if (sendto(sockbrcast, hellomesg, msgsize, 0,
		   (struct sockaddr *)&addrbrcast_snd,
		   sizeof(addrbrcast_snd)) < 0) {
		perror("Sending broadcast");
		close(sockbrcast);
		exit(EXIT_FAILURE);
	}

	// Делаем прослушивание сокета broadcast'ов неблокирующим
	fcntl(sockbrcast, F_SETFL, O_NONBLOCK);

	// Создаем множество прослушивания
	fd_set readset;
	FD_ZERO(&readset);
	FD_SET(sockbrcast, &readset);

	// Таймаут
	struct timeval timeout;
	timeout.tv_sec = 3;
	timeout.tv_usec = 0;

	struct sockaddr_in *servers =
		(struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
	bzero(servers, sizeof(struct sockaddr_in));
	int servcount = 0;
	int maxserv = 1;
	socklen_t servaddrlen = sizeof(struct sockaddr_in);
	// Создаем список серверов servers
	while (select(sockbrcast + 1, &readset, NULL, &readset, &timeout) > 0) {
		int rdbyte = recvfrom(sockbrcast, (void *)hellomesg, msgsize,
				      MSG_TRUNC,
				      (struct sockaddr *)&servers[servcount],
				      &servaddrlen);
		if (rdbyte == msgsize &&
		    strcmp(hellomesg, "Hello Client") == 0) {
			servcount++;

			if (servcount >= maxserv) {
				servers = realloc(servers,
						  sizeof(struct sockaddr_in) *
							  (maxserv + 1));
				if (servers == NULL) {
					perror("Realloc failed");
					close(sockbrcast);
					exit(EXIT_FAILURE);
				}
				bzero(&servers[servcount], servaddrlen);
				maxserv++;
			}
			FD_ZERO(&readset);
			FD_SET(sockbrcast, &readset);
		}
	}
	int i;
	if (servcount < 1) {
		fprintf(stderr, "No servers found!\n");
		exit(EXIT_FAILURE);
	}
	if (argc > 3 && maxservu <= servcount)
		servcount = maxservu;
	for (i = 0; i < servcount; ++i) {
		printf("Server answer from %s on port %d\n",
		       inet_ntoa(servers[i].sin_addr),
		       ntohs(servers[i].sin_port));
	}
	printf("\n");
	free(hellomesg);

	if (servcount > 2 * array_size) {
		servcount = array_size / 2;
	}
	long long *results = (long long *)malloc(sizeof(long long) * servcount);
	// Создаем треды для работы с серверами
	pthread_t *tid = (pthread_t *)malloc(sizeof(pthread_t) * servcount);
	//
	int count_per_server = array_size / servcount;
	for (i = 0; i < servcount; ++i) {
		thread_args_t *args =
			(thread_args_t *)malloc(sizeof(thread_args_t));
		if (i < servcount - 1) {
			args->elements_count = count_per_server;
		} else {
			args->elements_count =
				array_size - i * count_per_server;
		}
		args->elements = digits + i * count_per_server;
		args->results = &results[i];
		args->server = &servers[i];

		if (pthread_create(&tid[i], NULL, send_thread, args) != 0) {
			perror("Create send thread failed");
			exit(EXIT_FAILURE);
		}
	}
	// Ждем все сервера
	for (i = 0; i < servcount; ++i)
		pthread_join(tid[i], NULL);

	// Вычисляем результат
	long long res = results[0];
	for (i = 1; i < servcount; ++i)
		res = results[i] > res ? results[i] : res;

	free(servers);
	free(digits);
	printf("\nResult: %lld\n", res);
	return (EXIT_SUCCESS);
}
