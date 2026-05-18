#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define PENS 0
#define FOME 1
#define COME 2

#define ESQUERDA (i + N - 1) % N
#define DIREITA (i + 1) % N

int N;                    
int *estado;              
int *refeicoes;           
int *garfos;              

sem_t *sem_filosofos; // cada fil. tem um semaforo, faz dormir e acordar 
pthread_mutex_t mutex;    //apenas um filosofo muda de estado por vez

struct timeval tempo_inicio; 
int simulacao_ativa = 1;     

typedef struct {
    int id;
    int min_pensar;
    int max_pensar;
    int min_comer;
    int max_comer;
    unsigned int seed; 
} FilosArgs;

void obter_tempo_atual(char *buffer) {
    struct timeval agora;
    gettimeofday(&agora, NULL);
    long segundos = agora.tv_sec - tempo_inicio.tv_sec;
    long microsegundos = agora.tv_usec - tempo_inicio.tv_usec;
    if (microsegundos < 0) { segundos -= 1; microsegundos += 1000000; }
    sprintf(buffer, "[%02ld:%02ld:%02ld.%03ld]", segundos/3600, (segundos%3600)/60, segundos%60, microsegundos/1000);
}

const char* nome_estado(int e) {
    if (e == PENS) return "PENS";
    if (e == FOME) return "FOME";
    if (e == COME) return "COME";
    return "ERRO";
}

void imprimir_estado(int id_filosofo, const char* transicao) {
    char tempo[25];
    obter_tempo_atual(tempo);

    printf("%s F%d: %s\n", tempo, id_filosofo, transicao);
    printf("  Garfos: ");
    for (int i = 0; i < N; i++) {
        if (garfos[i] == 1) printf("[X] ");
        else printf("[O] ");
    }
    printf("\n  Filósofos: ");
    for (int i = 0; i < N; i++) {
        printf("F%d:%s", i, nome_estado(estado[i]));
        if (i < N - 1) printf(" | ");
    }
    printf("\n  Refeições: ");
    for (int i = 0; i < N; i++) {
        printf("F%d:%d", i, refeicoes[i]);
        if (i < N - 1) printf(" | ");
    }
    printf("\n\n"); 
}


void esperar_aleatorio(unsigned int *seed, int min_ms, int max_ms) {
    int tempo_ms = min_ms + rand_r(seed) % (max_ms - min_ms + 1);
    usleep(tempo_ms * 1000); 
}

void testar(int i) {
    if (estado[i] == FOME && estado[ESQUERDA] != COME && estado[DIREITA] != COME) {
        estado[i] = COME;
        refeicoes[i]++; 
        garfos[ESQUERDA] = 1; 
        garfos[i] = 1;        
        imprimir_estado(i, "FOME -> COME"); 
        sem_post(&sem_filosofos[i]); 
    }
}

void pegar_garfos(int i) {
    pthread_mutex_lock(&mutex); 
    estado[i] = FOME;
    imprimir_estado(i, "PENS -> FOME"); 
    testar(i); 
    pthread_mutex_unlock(&mutex); 
    sem_wait(&sem_filosofos[i]); 
}

void devolver_garfos(int i) {
    pthread_mutex_lock(&mutex); 
    estado[i] = PENS;
    garfos[ESQUERDA] = 0;
    garfos[i] = 0;
    imprimir_estado(i, "COME -> PENS"); 
    testar(ESQUERDA);
    testar(DIREITA);
    pthread_mutex_unlock(&mutex); 
}

void* rotina_filosofo(void* arg) {
    FilosArgs* dados = (FilosArgs*) arg;
    int i = dados->id;
    while (simulacao_ativa) {
        esperar_aleatorio(&dados->seed, dados->min_pensar, dados->max_pensar);
        if (!simulacao_ativa) break; 
        
        pegar_garfos(i);
        if (!simulacao_ativa) break; // Trava extra para garantir que ele não coma se o tempo acabou
        
        esperar_aleatorio(&dados->seed, dados->min_comer, dados->max_comer);
        devolver_garfos(i);
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 7) {
        printf("Uso correto: %s <N> <Tempo_Simulacao_s> <Min_Pensar_ms> <Max_Pensar_ms> <Min_Comer_ms> <Max_Comer_ms>\n", argv[0]);
        return 1;
    }

    N = atoi(argv[1]);
    int tempo_simulacao = atoi(argv[2]);
    int min_p = atoi(argv[3]);
    int max_p = atoi(argv[4]);
    int min_c = atoi(argv[5]);
    int max_c = atoi(argv[6]);

    if (N < 3) {
        printf("O numero de filosofos deve ser pelo menos 3.\n");
        return 1;
    }

    estado = (int*) malloc(N * sizeof(int));
    refeicoes = (int*) calloc(N, sizeof(int)); 
    garfos = (int*) calloc(N, sizeof(int));
    sem_filosofos = (sem_t*) malloc(N * sizeof(sem_t));

    pthread_t threads[N];
    FilosArgs args[N];

    pthread_mutex_init(&mutex, NULL);
    gettimeofday(&tempo_inicio, NULL);

    for (int i = 0; i < N; i++) {
        estado[i] = PENS;
        sem_init(&sem_filosofos[i], 0, 0); 
        args[i].id = i;
        args[i].min_pensar = min_p;
        args[i].max_pensar = max_p;
        args[i].min_comer = min_c;
        args[i].max_comer = max_c;
        args[i].seed = time(NULL) ^ i; 
        pthread_create(&threads[i], NULL, rotina_filosofo, (void*)&args[i]);
    }

    sleep(tempo_simulacao);
    simulacao_ativa = 0;

    // ACORDANDO QUEM FICOU PRESO (A correção brilhante)
    for (int i = 0; i < N; i++) {
        sem_post(&sem_filosofos[i]);
    }

    for (int i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("--- RESUMO FINAL ---\n");
    for (int i = 0; i < N; i++) {
        printf("F%d comeu: %d vezes\n", i, refeicoes[i]);
    }

    pthread_mutex_destroy(&mutex);
    for (int i = 0; i < N; i++) sem_destroy(&sem_filosofos[i]);
    free(estado); free(refeicoes); free(garfos); free(sem_filosofos);

    return 0;
}