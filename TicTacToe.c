/*Wojciech Duklas projekt*/
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<errno.h>
#include<arpa/inet.h>
#include<netdb.h>
#include<netinet/in.h>
#include<unistd.h>
#include<sys/wait.h>
#include<sys/ipc.h>
#include<sys/shm.h>

#include<signal.h> /*ctrl + Z*/

key_t shmkey;
int shmid;
char  *shared_data;

/*stuktura TTT - Tic Tac Toe */
struct TTT{
	char    board[9];
	char    mark_1;
	char    mark_2;
	int     score_1;
	int     score_2;
	int     round;
	char    nick_opponent[16];
	int     counter;
} *game;

void initialize_board();        /*wstawia poczatkowe wartosci do tablicy*/
void board();                   /*wysweitla tablice*/
void sgnhandle(int signal);     /*czysci po grze*/
int win();                      /*sprawdza czy ktos wygral*/
void US2(int signal);           /*AWARYJNE WYglaczenie gry ctrl z*/


int main(int argc, char *argv[])
{
	struct hostent *host; /*potrzebne do uzyskania ip*/
	struct in_addr *addr;
	char *ip4;
	struct sockaddr_in c_addr; /*do gniazd*/
    struct sockaddr_in h_addr;
	int child, sockfd;
	char mode;
	char pr_1[7]; /*nick gracza 1*/

	struct addrinfo * pi_addr;
	struct sockaddr_in client_addr, *p_addr;

    signal(SIGTSTP, US2); /* ctrl + Z*/
    signal(SIGINT, sgnhandle);
	if(argc == 1 || argc > 3){
		printf("Plik nie poprawnie uruchomiony.\nPoprawne uzycie:\n1.Adres domenowy lub ip maszyny z ktora chcemy zagrac, opcjonalnie Twoj Nick\nPrzyklad: ultra60 Grzesiek\n");
		return EXIT_FAILURE;
	}

    if (getaddrinfo(argv[1], "7010", NULL, &pi_addr) != 0){
		perror("getaddrinfo");
		return EXIT_FAILURE;
    }

    p_addr = (struct sockaddr_in *)(pi_addr->ai_addr);

    host = gethostbyname(argv[1]);
	if(host == NULL) {
		perror("Blad gethostbyname");
		return EXIT_FAILURE;
	}
	addr = (struct in_addr *) *host->h_addr_list;
	ip4 = inet_ntoa(*addr);


    /*przygotowywanie serwera*/
	h_addr.sin_family      = AF_INET;
	h_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	h_addr.sin_port        = htons(7010);

	sockfd = socket(AF_INET, SOCK_DGRAM, 0);           /*gniazdo UDP*/

	if(bind(sockfd, (struct sockaddr *) &h_addr, sizeof(h_addr)) == -1){
		perror("bind");
        return EXIT_FAILURE;
	}

    /*czas na klienta, gracza nr 2*/
	c_addr.sin_family  = AF_INET;              //IPv4
	//inet_aton(ip4, &c_addr.sin_addr); /*robi to samo co linijka nizej, ale nie daje 2pkt
	c_addr.sin_addr    = p_addr->sin_addr;
	c_addr.sin_port    = htons(7010);          //port w porzadku sieciowym

	char peer_addr_hr[INET_ADDRSTRLEN];             //adres peera human readable
	if(inet_ntop(AF_INET, &ip4, peer_addr_hr, INET_ADDRSTRLEN) == NULL){
		perror("inet_ntop");
        return EXIT_FAILURE;
    }
	shmkey = ftok(argv[1], 1);  //klucz do generacji pamiedzi dzielonej

	if((shmid = shmget(shmkey, sizeof(struct TTT), 0600 | IPC_CREAT | IPC_EXCL)) == -1){
        perror("shmget");
        return EXIT_FAILURE;
    }
	game = (struct TTT *)shmat(shmid, (void*) 0, 0);
	if(game == (struct TTT *) - 1){
		perror("shmat");
        return EXIT_FAILURE;
	}

    printf("Rozpoczynam gre z %s. Napisz k by zakonczyc.\n", ip4);
    /*Wpisywanie danych do struktory*/
	initialize_board();
	game->score_1 = 0;
	game->score_2 = 0;
	game->mark_1 = 'X';
	game->mark_2 = 'O';
	game->counter = 0;
	/*Nazwa gracza*/
    if (argc == 3){
		strncpy(pr_1, argv[2], 8);
		pr_1[7] = '\0';
	}
	else strcpy(pr_1, "NN");
	/*nazwa gracza drugiego gdyby nie podal*/
    strncpy(game->nick_opponent, "NN", 3);
    mode = 'p';
    /*  p - play - granie dalej
        w - pokaz wynik
        k - koniec programu*/


	if (sendto(sockfd, &mode, sizeof(mode), 0, (struct sockaddr *) &c_addr, sizeof(c_addr)) == -1){
		perror("sendto");
        return EXIT_FAILURE;
    }

	if (sendto(sockfd, &pr_1, sizeof(pr_1), 0, (struct sockaddr *) &c_addr, sizeof(c_addr)) == -1){
		perror("sendto");
		return EXIT_FAILURE;
    }


	if((child = fork()) == 0){
		if(connect(sockfd, (struct sockaddr *) &c_addr, sizeof(c_addr)) == -1){
			perror("connect");
			return EXIT_FAILURE;
        }
		while(1){
			if(read(sockfd, &mode, sizeof(mode)) == -1){
				perror("read");
				return EXIT_FAILURE;
            }
            if (mode == 'k'){/*koniec programu - wiadomosc*/
				printf("\n[%s (%s) zakonczyl gre]\n", game->nick_opponent, ip4);
            }else if(mode == 'p'){
				if (read(sockfd, &(game->nick_opponent), sizeof(game->nick_opponent)) == -1){
					perror("read");
                    return EXIT_FAILURE;
				}
				printf("\n[%s (%s) dolaczyl do gry]\n", game->nick_opponent, ip4);
				board();
				game->mark_1 = 'O';
				game->mark_2 = 'X';
				game->round = 1;
				write(1, "[wybierz pole] ", 15);
			}
			else{
				printf("\n[%s (%s) wybral pole %c]\n", game->nick_opponent, ip4, mode);
				game->board[mode - 97] = game->mark_2;
				game->round = 1;
				board();
				if(win() == 1){
					game->score_2 += 1;
					write(1, "[Pregrana! Zagraj jeszcze raz]\n", 32);
					initialize_board();
					board();
					game->counter = 0;
				}
				if(game->counter == 5){
                        printf("Remis\n");
                        initialize_board();
                        board();
                        game->counter = 0;
					}
				write(1, "[wybierz pole] ", 15);
			}
		}
		exit(EXIT_FAILURE);
	}
	else{
		if(connect(sockfd, (struct sockaddr *) &c_addr, sizeof(c_addr)) == -1){
			perror("connect");
			return EXIT_FAILURE;
        }
		printf("[Propozycja gry wyslana]\n");
		while(1){
			scanf(" %c", &mode);

			if (mode == 'w'){
            printf("Ty %d : %d %s\n", game->score_1, game->score_2, game->nick_opponent);
			}
            else if (mode == 'k'){/*koniec programu*/
				if (write(sockfd, &mode, sizeof(mode)) == -1){
					perror("write");
                    return EXIT_FAILURE;
                }
				kill(child, SIGINT);
				close(sockfd);
				exit(EXIT_SUCCESS);
			}
			else{
				if (game->round == 1){
					while (mode - 97 < 0 || mode - 97 > 8 || game->board[mode - 97] == 'X' || game->board[mode - 97] == 'O'){
						printf("[tego pola nie mozesz wybrac, wybierz pole] ");

						scanf(" %c", &mode);
					}
                    game->counter += 1;
                    printf("Runda: %d\n", game->counter);
					game->board[mode - 97] = game->mark_1;
					if (win() == 1){
						game->score_1 += 1;
						printf("[Wygrana! Kolejna rozgrywka, poczekaj na swoja kolej] ");
						initialize_board();
                        game->counter = 0;
					}
					if(game->counter == 5){
                        printf("Remis\n");
                        initialize_board();
                        game->counter = 0;
					}
					if (write(sockfd, &mode, sizeof(mode)) == -1){
						perror("write");
                        return EXIT_FAILURE;
                    }
					game->round = 0;
				}else if (game->round == 0) printf("[teraz kolej drugiego gracza, poczekaj na swoja kolej]\n");
			}
		}
	}
	return 0;
}

/*wstawia poczatkowe wartosci do tablicy*/
void initialize_board(){
	int i;
	for(i = 0; i < 9; i++) game->board[i] = 'a' + i;
}
/*wysweitla tablice*/
void board(){
	int i;
	for(i=0; i < 9; i++){
		printf("%c", game->board[i]);
		if(i == 0 || i == 1 || i == 3 || i == 4 || i == 6 || i == 7) printf(" | ");
		if(i == 2 || i == 5 || i == 8) printf("\n");
	}
}
/*czysci po grze*/
void sgnhandle(int signal) {
	printf("\n[Serwer]: dostalem SIGINT => koncze i sprzatam...");
	printf(" (odlaczenie: %s, usuniecie: %s)\n",
			(shmdt(shared_data) == 0)        ?"OK":"blad shmdt",
			(shmctl(shmid, IPC_RMID, 0) == 0)?"OK":"blad shmctl");
	exit(0);
}
/*sprawdza czy ktos wygral*/
int win(){
    int i;
    /*linie wygrywajace pionowe i poziome*/
    for(i = 3; i<=5; i++) if(game->board[i-3] == game->board[i] && game->board[i-3] == game->board[i+3]) return 1;
    for(i = 1; i<=7; i+=3) if(game->board[i-1] == game->board[i] && game->board[i-1] == game->board[i+1]) return 1;
    /*linie wygrywajace skosne*/
	if(game->board[0] == game->board[4] && game->board[4] == game->board[8]) return 1;
	if(game->board[2] == game->board[4] && game->board[4] == game->board[6]) return 1;

	return 0;
}
void US2(int signal){
	printf("DOSTALEM SYGNAL US2, czyli CTRL + Z\n");
	printf("DLatego zamykam program\n");
	exit(0);
}
