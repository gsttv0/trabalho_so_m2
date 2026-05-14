#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

// Definindo os estados possíveis
#define PENS 0
#define FOME 1
#define COME 2

// Como a mesa é redonda, usamos módulo para achar os vizinhos
#define ESQUERDA (i + N - 1) % N
#define DIREITA (i + 1) % N

// Variáveis Globais
int N;                    // Número de filósofos
int *estado;              // Vetor de estados (PENS, FOME, COME)
int *refeicoes;           // Contador de quantas vezes cada um comeu
int *garfos;              // Vetor para desenhar os garfos [O] ou [X]

sem_t *sem_filosofos;     // Vetor de semáforos (um para cada filósofo)
pthread_mutex_t mutex;    // Mutex para proteger a alteração de estados e os prints

struct timeval tempo_inicio; // Marca a hora exata que a simulação começou
int simulacao_ativa = 1;     // Flag global para encerrar a simulação

// Estrutura do "Envelope" (Mochila) que cada Thread vai receber
typedef struct {
    int id;
    int min_pensar;
    int max_pensar;
    int min_comer;
    int max_comer;
} FilosArgs;

// =========================================================================
// FUNÇÕES AUXILIARES DE TEMPO E IMPRESSÃO
// =========================================================================

// Retorna uma string com o formato [HH:MM:SS.mmm]
void obter_tempo_atual(char *buffer) {
    struct timeval agora;
    gettimeofday(&agora, NULL);
    
    long segundos = agora.tv_sec - tempo_inicio.tv_sec;
    long microsegundos = agora.tv_usec - tempo_inicio.tv_usec;
    
    if (microsegundos < 0) {
        segundos -= 1;
        microsegundos += 1000000;
    }
    
    long milissegundos = microsegundos / 1000;
    long horas = segundos / 3600;
    long minutos = (segundos % 3600) / 60;
    long segs_restantes = segundos % 60;
    
    sprintf(buffer, "[%02ld:%02ld:%02ld.%03ld]", horas, minutos, segs_restantes, milissegundos);
}

const char* nome_estado(int e) {
    if (e == PENS) return "PENS";
    if (e == FOME) return "FOME";
    if (e == COME) return "COME";
    return "ERRO";
}

// Imprime a "fotografia" exata da mesa (Sempre chamada dentro do Mutex!)
void imprimir_estado(int id_filosofo, const char* transicao) {
    char tempo[25];
    obter_tempo_atual(tempo);

    printf("%s F%d: %s\n", tempo, id_filosofo, transicao);

    printf("  Garfos: ");
    for (int i = 0; i < N; i++) {
        if (garfos[i] == 1) printf("[X] ");
        else printf("[O] ");
    }
    printf("\n");

    printf("  Filósofos: ");
    for (int i = 0; i < N; i++) {
        printf("F%d:%s", i, nome_estado(estado[i]));
        if (i < N - 1) printf(" | ");
    }
    printf("\n");

    printf("  Refeições: ");
    for (int i = 0; i < N; i++) {
        printf("F%d:%d", i, refeicoes[i]);
        if (i < N - 1) printf(" | ");
    }
    printf("\n\n"); 
}

void esperar_aleatorio(int min_ms, int max_ms) {
    int tempo_ms = min_ms + rand() % (max_ms - min_ms + 1);
    usleep(tempo_ms * 1000); 
}

// =========================================================================
// LÓGICA DE SINCRONIZAÇÃO (DIJKSTRA)
// =========================================================================

void testar(int i) {
    if (estado[i] == FOME && estado[ESQUERDA] != COME && estado[DIREITA] != COME) {
        estado[i] = COME;
        refeicoes[i]++; 
        
        garfos[ESQUERDA] = 1; 
        garfos[i] = 1;        
        
        imprimir_estado(i, "FOME > COME");
        
        sem_post(&sem_filosofos[i]); // Libera o filósofo para comer
    }
}

void pegar_garfos(int i) {
    pthread_mutex_lock(&mutex); 
    
    estado[i] = FOME;
    imprimir_estado(i, "PENS > FOME");
    testar(i); 
    
    pthread_mutex_unlock(&mutex); 
    
    sem_wait(&sem_filosofos[i]); // Bloqueia se não conseguiu comer
}

void devolver_garfos(int i) {
    pthread_mutex_lock(&mutex); 
    
    estado[i] = PENS;
    garfos[ESQUERDA] = 0;
    garfos[i] = 0;
    
    imprimir_estado(i, "COME > PENS");
    
    // Acorda os vizinhos se eles estiverem com fome
    testar(ESQUERDA);
    testar(DIREITA);
    
    pthread_mutex_unlock(&mutex); 
}

// =========================================================================
// ROTINA DA THREAD E MAIN
// =========================================================================

void* rotina_filosofo(void* arg) {
    FilosArgs* dados = (FilosArgs*) arg;
    int i = dados->id;

    while (simulacao_ativa) {
        esperar_aleatorio(dados->min_pensar, dados->max_pensar);
        if (!simulacao_ativa) break; 

        pegar_garfos(i);
        esperar_aleatorio(dados->min_comer, dados->max_comer);
        devolver_garfos(i);
    }
    
    pthread_exit(NULL);
}

int main() {
    int tempo_simulacao;
    int min_p, max_p, min_c, max_c;

    printf("Digite os parametros (N Tempo_Segundos Min_Pensar Max_Pensar Min_Comer Max_Comer):\n");
    if (scanf("%d %d %d %d %d %d", &N, &tempo_simulacao, &min_p, &max_p, &min_c, &max_c) != 6) {
        printf("Erro na leitura dos dados.\n");
        return 1;
    }

    if (N < 3) {
        printf("O numero de filosofos deve ser pelo menos 3.\n");
        return 1;
    }

    // Alocação dinâmica
    estado = (int*) malloc(N * sizeof(int));
    refeicoes = (int*) calloc(N, sizeof(int)); 
    garfos = (int*) calloc(N, sizeof(int));
    sem_filosofos = (sem_t*) malloc(N * sizeof(sem_t));

    pthread_t threads[N];
    FilosArgs args[N];

    pthread_mutex_init(&mutex, NULL);
    gettimeofday(&tempo_inicio, NULL);
    srand(time(NULL));

    // Cria as threads [cite: 26]
    for (int i = 0; i < N; i++) {
        estado[i] = PENS;
        sem_init(&sem_filosofos[i], 0, 0); 
        
        args[i].id = i;
        args[i].min_pensar = min_p;
        args[i].max_pensar = max_p;
        args[i].min_comer = min_c;
        args[i].max_comer = max_c;
        
        pthread_create(&threads[i], NULL, rotina_filosofo, (void*)&args[i]); [cite: 4]
    }

    // Aguarda o tempo de simulação
    sleep(tempo_simulacao);
    simulacao_ativa = 0;

    // Aguarda o fim das threads [cite: 4]
    for (int i = 0; i < N; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("--- RESUMO FINAL ---\n"); [cite: 85-86]
    for (int i = 0; i < N; i++) {
        printf("F%d comeu: %d vezes\n", i, refeicoes[i]);
    }

    // Limpeza da memória
    pthread_mutex_destroy(&mutex);
    for (int i = 0; i < N; i++) sem_destroy(&sem_filosofos[i]);
    free(estado); free(refeicoes); free(garfos); free(sem_filosofos);

    return 0;
}