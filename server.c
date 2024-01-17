#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define RCVPORT 38199
#define FUNC(x) x *x // В примере я буду вычислять интеграл от x^2

// Структура для передачи задания серверу
typedef struct {
	int limits;
	int numoftry;
} task_data_t;

// Аргумент функции треда вычислителя
typedef struct {
	int limits; // Предел интегрирования
	long numoftry; // Количество попыток которые должен выполнить тред
	long double *results; // Куда записать результат
} thread_args_t;

// Функция треда вычислителя
void *calculate(void *arg)
{
	// Ожидаем в качестве аргумента указатель на структуру thread_args_t
	thread_args_t *tinfo = (thread_args_t *)arg;
	long double result = 0;
	int xlim = tinfo->limits;
	int trys = tinfo->numoftry;
	unsigned a = xlim;
	for (int i = 0; i < trys; ++i) {
		int div = rand_r(&a);
		int div2 = rand_r(&a);
		double x = div % xlim + (div2 / (div2 * 1.0 + div * 1.0));
		result += FUNC(x);
	}
	*(tinfo->results) = result;
	return NULL;
}

// Аргумент для проверяющего клиента треда
typedef struct {
	int sock; // Сокет с клиентом
	pthread_t *calcthreads; // Треды которые в случае чего надо убить
	int threadnum; // Количество этих тредов
} checker_args_t;

// Функция которая будет выполнена тредом получившим сигнал SIGUSR1
void thread_cancel(int signo)
{
	pthread_exit(PTHREAD_CANCELED);
}

// Тред проверяющий состояние клиента
void *client_check(void *arg)
{
	// Нам должен быть передан аргумент типа checker_args_t
	checker_args_t *args = (checker_args_t *)arg;
	char a[10];
	recv(args->sock, &a, 10,
	     0); // Так как мы используем TCP, если клиент умрет или что либо
		// скажет, то recv тут же разблокирует тред и вернёт -1
	int st;
	for (int i = 0; i < args->threadnum; ++i)
		st = pthread_kill(args->calcthreads[i],
				  SIGUSR1); // Шлем всем вычислителям SIGUSR1
	return NULL;
}

void *listen_broadcast(void *arg)
{
	int *isbusy = arg;
	// Создаем сокет для работы с broadcast
	int sockbrcast = socket(PF_INET, SOCK_DGRAM, 0);
	if (sockbrcast == -1) {
		perror("Create broadcast socket failed");
		exit(EXIT_FAILURE);
	}

	// Создаем структуру для приема ответов на broadcast
	int port_rcv = RCVPORT;
	struct sockaddr_in addrbrcast_rcv;
	bzero(&addrbrcast_rcv, sizeof(addrbrcast_rcv));
	addrbrcast_rcv.sin_family = AF_INET;
	addrbrcast_rcv.sin_addr.s_addr = htonl(INADDR_ANY);
	addrbrcast_rcv.sin_port = htons(port_rcv);
	// Биндим её
	if (bind(sockbrcast, (struct sockaddr *)&addrbrcast_rcv,
		 sizeof(addrbrcast_rcv)) < 0) {
		perror("Bind broadcast socket failed");
		close(sockbrcast);
		exit(EXIT_FAILURE);
	}

	int msgsize = sizeof(char) * 18;
	char hellomesg[18];
	bzero(hellomesg, msgsize);
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

	struct sockaddr_in client;
	;
	bzero(&client, sizeof(client));
	socklen_t servaddrlen = sizeof(struct sockaddr_in);
	char helloanswer[18];
	bzero(helloanswer, msgsize);
	strcpy(helloanswer, "Hello Client");
	int sockst = 1;
	while (sockst > 0) {
		sockst = select(sockbrcast + 1, &readset, NULL, &readset, NULL);
		if (sockst == -1) {
			perror("Broblems on broadcast socket");
			exit(EXIT_FAILURE);
		}
		int rdbyte = recvfrom(sockbrcast, (void *)hellomesg, msgsize,
				      MSG_TRUNC, (struct sockaddr *)&client,
				      &servaddrlen);
		if (rdbyte == msgsize &&
		    strcmp(hellomesg, "Hello Integral") == 0 && *isbusy == 0) {
			if (sendto(sockbrcast, helloanswer, msgsize, 0,
				   (struct sockaddr *)&client,
				   sizeof(struct sockaddr_in)) < 0) {
				perror("Sending answer");
				close(sockbrcast);
				exit(EXIT_FAILURE);
			}
		}
		FD_ZERO(&readset);
		FD_SET(sockbrcast, &readset);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	// Аргумент может быть только один - это кол-во тредов
	if (argc > 2) {
		fprintf(stderr, "Usage: %s [numofcpus]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int numofthread;

	if (argc == 2) {
		numofthread = atoi(argv[1]);
		if (numofthread < 1) {
			fprintf(stderr, "Incorrect num of threads!\n");
			exit(EXIT_FAILURE);
		}
		fprintf(stdout, "Num of threads forced to %d\n", numofthread);
	} else {
		// Если аргументов нет, то определяем кол-во процессоров автоматически
		numofthread = sysconf(_SC_NPROCESSORS_ONLN);
		if (numofthread < 1) {
			fprintf(stderr, "Can't detect num of processors\n"
					"Continue in two threads\n");
			numofthread = 2;
		}
		fprintf(stdout,
			"Num of threads detected automatically it's %d\n\n",
			numofthread);
	}

	struct sigaction cancel_act;
	memset(&cancel_act, 0, sizeof(cancel_act));
	cancel_act.sa_handler = thread_cancel;
	sigfillset(&cancel_act.sa_mask);
	sigaction(SIGUSR1, &cancel_act, NULL);

	// Создаем тред слушающий broadcast'ы
	pthread_t broadcast_thread;
	int isbusy = 1; //(int*) malloc(sizeof(int));
	// Переменная которая сообщает треду следует ли отвечать на broadcast
	// 0 - отвечать, 1- нет
	isbusy = 1;
	if (pthread_create(&broadcast_thread, NULL, listen_broadcast,
			   &isbusy)) {
		fprintf(stderr, "Can't create broadcast listen thread");
		perror("Detail:");
		exit(EXIT_FAILURE);
	}

	int listener;
	struct sockaddr_in addr;
	listener = socket(PF_INET, SOCK_STREAM, 0);
	if (listener < 0) {
		perror("Can't create listen socket");
		exit(EXIT_FAILURE);
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(RCVPORT);
	addr.sin_addr.s_addr = INADDR_ANY;
	int a = 1;
	// Добавляем опцию SO_REUSEADDR для случаев когда мы перезапускам сервер
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &a, sizeof(a)) < 0) {
		perror("Set listener socket options");
		exit(EXIT_FAILURE);
	}

	// Биндим сокет
	if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("Can't bind listen socket");
		exit(EXIT_FAILURE);
	}

	// Начинаем ждать соединения от клиентов
	if (listen(listener, 1) < 0) {
		perror("Eror listen socket");
		exit(EXIT_FAILURE);
	}

	int needexit = 0;
	while (needexit == 0) {
		fprintf(stdout, "\nWait new connection...\n\n");
		int client;
		isbusy = 0; // Разрешаем отвечать клиентам на запросы
		struct sockaddr_in addrclient;
		socklen_t addrclientsize = sizeof(addrclient);
		client = accept(listener, (struct sockaddr *)&addrclient,
				&addrclientsize);
		if (client < 0) {
			perror("Client accepting");
		}

		isbusy = 1; // Запрещаем отвечать на запросы
		task_data_t data;
		int read_bytes = recv(client, &data, sizeof(data), 0);
		if (read_bytes != sizeof(data) || data.limits < 1 ||
		    data.numoftry < 1) {
			fprintf(stderr,
				"Invalid data from %s on port %d, reset peer\n",
				inet_ntoa(addrclient.sin_addr),
				ntohs(addrclient.sin_port));
			close(client);
			isbusy = 0;
		} else {
			fprintf(stdout,
				"New task from %s on port %d\nlimits: %d\n"
				"numoftrys: %d\n",
				inet_ntoa(addrclient.sin_addr),
				ntohs(addrclient.sin_port), data.limits,
				data.numoftry);
			thread_args_t *tinfo;
			pthread_t *calc_threads = (pthread_t *)malloc(
				sizeof(pthread_t) * numofthread);
			int threads_trys = data.numoftry % numofthread;
			long double *results = (long double *)malloc(
				sizeof(long double) * numofthread);
			tinfo = (thread_args_t *)malloc(sizeof(thread_args_t) *
							numofthread);
			// Создаем вычислительные треды
			int numofthreadtry = data.numoftry / numofthread + 1;
			for (int i = 0; i < numofthread; ++i) {
				tinfo[i].limits = data.limits;
				tinfo[i].numoftry = numofthreadtry;
				tinfo[i].results = &results[i];
				if (pthread_create(&calc_threads[i], NULL,
						   calculate, &tinfo[i]) != 0) {
					fprintf(stderr,
						"Can't create thread by num %d",
						i);
					perror("Detail:");
					exit(EXIT_FAILURE);
				}
			}

			// Создаем тред проверяющий соединение с клиентом
			checker_args_t checker_arg;
			checker_arg.calcthreads = calc_threads;
			checker_arg.threadnum = numofthread;
			checker_arg.sock = client;
			pthread_t checker_thread;
			if (pthread_create(&checker_thread, NULL, client_check,
					   &checker_arg) != 0) {
				fprintf(stderr, "Can't create checker thread");
				perror("Detail:");
				exit(EXIT_FAILURE);
			}
			int iscanceled = 0; // Почему завершились треды?
			int *exitstat;
			for (int i = 0; i < numofthread; ++i) {
				pthread_join(calc_threads[i],
					     (void *)&exitstat);
				if (exitstat == PTHREAD_CANCELED)
					iscanceled = 1; // Отменили их
			}
			if (iscanceled != 1) {
				long double *res = (long double *)malloc(
					sizeof(long double));
				bzero(res, sizeof(long double));
				*res = 0.0;
				for (int i = 0; i < numofthread; ++i)
					*res += results[i];
				pthread_kill(checker_thread, SIGUSR1);
				if (send(client, res, sizeof(long double), 0) <
				    0) {
					perror("Sending error");
				}
				close(client);
				free(res);
				//free(checker_arg);
				free(results);
				free(calc_threads);
				free(tinfo);
				isbusy = 0;
				fprintf(stdout, "Calculate and send finish!\n");
			} else {
				fprintf(stderr, "Client die!\n");
				close(client);
				//free(checker_arg);
				free(results);
				free(calc_threads);
				free(tinfo);
			}
		}
	}

	return (EXIT_SUCCESS);
}
