#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define DORME 0
#define ATENDE 1

int N_cadeiras;
int taxa_chegada;
int tempo_atendimento;
int tempo_simulacao;

int clientes_atendidos = 0;
int clientes_desistentes = 0;
int clientes_em_espera = 0;

int *fila_clientes; 
int pos_in = 0;     
int pos_out = 0;    

int barbeiro_estado = DORME;
int id_cliente_cadeira = -1; 

pthread_mutex_t mutex_fila; 
sem_t sem_clientes;         

struct timeval tempo_inicio;
int simulacao_ativa = 1;

void obter_tempo_atual(char *buffer) {
    struct timeval agora;
    gettimeofday(&agora, NULL);
    long segundos = agora.tv_sec - tempo_inicio.tv_sec;
    long microsegundos = agora.tv_usec - tempo_inicio.tv_usec;
    if (microsegundos < 0) { segundos -= 1; microsegundos += 1000000; }
    sprintf(buffer, "[%02ld:%02ld:%02ld.%03ld]", segundos/3600, (segundos%3600)/60, segundos%60, microsegundos/1000);
}

// Adicionada a semente para segurança de threads
void esperar_aleatorio(unsigned int *seed, int tempo_medio) {
    int min = tempo_medio / 2;
    int max = tempo_medio + min;
    int tempo_ms = min + rand_r(seed) % (max - min + 1);
    usleep(tempo_ms * 1000);
}

void imprimir_evento(const char* mensagem) {
    char tempo[25];
    obter_tempo_atual(tempo);

    printf("%s %s\n", tempo, mensagem);
    if (barbeiro_estado == DORME) printf("Barbeiro: DORME\n");
    else printf("Barbeiro: ATENDE C%d\n", id_cliente_cadeira);

    printf("Fila: [");
    for(int i = 0; i < N_cadeiras; i++) {
        if (i < clientes_em_espera) printf("#");
        else printf(".");
    }
    printf("] (%d/%d) -> ", clientes_em_espera, N_cadeiras);
    
    int idx = pos_out;
    for(int i = 0; i < clientes_em_espera; i++) {
        printf("C%d ", fila_clientes[idx]);
        idx = (idx + 1) % N_cadeiras; 
    }
    printf("\n");
    printf("Contadores: atendidos = %d | desistentes = %d | em espera = %d\n\n", 
           clientes_atendidos, clientes_desistentes, clientes_em_espera);
}

void* rotina_barbeiro(void* arg) {
    char msg[100];
    unsigned int seed = time(NULL) ^ pthread_self();

    while (simulacao_ativa) {
        sem_wait(&sem_clientes);
        if (!simulacao_ativa && clientes_em_espera == 0) break;

        pthread_mutex_lock(&mutex_fila); 
        id_cliente_cadeira = fila_clientes[pos_out];
        pos_out = (pos_out + 1) % N_cadeiras;
        clientes_em_espera--;
        barbeiro_estado = ATENDE;
        
        sprintf(msg, "Barbeiro iniciou atendimento do cliente C%d", id_cliente_cadeira);
        imprimir_evento(msg);
        pthread_mutex_unlock(&mutex_fila); 

        esperar_aleatorio(&seed, tempo_atendimento);

        pthread_mutex_lock(&mutex_fila); 
        clientes_atendidos++;
        barbeiro_estado = DORME; 
        
        sprintf(msg, "Barbeiro concluiu atendimento do cliente C%d", id_cliente_cadeira);
        imprimir_evento(msg);
        pthread_mutex_unlock(&mutex_fila); 
    }
    pthread_exit(NULL);
}

void* rotina_cliente(void* arg) {
    int id = *((int*)arg);
    free(arg); 
    char msg[100];

    pthread_mutex_lock(&mutex_fila); 

    if (clientes_em_espera < N_cadeiras) {
        fila_clientes[pos_in] = id;
        pos_in = (pos_in + 1) % N_cadeiras;
        clientes_em_espera++;
        
        sprintf(msg, "Cliente C%d chegou e entrou na fila", id);
        imprimir_evento(msg);
        
        sem_post(&sem_clientes); 
        pthread_mutex_unlock(&mutex_fila); 
        
        // A thread do cliente encerra aqui (ele apenas pegou a senha da fila)
    } else {
        clientes_desistentes++;
        sprintf(msg, "Cliente C%d chegou, mas desistiu por falta de cadeira", id);
        imprimir_evento(msg);
        pthread_mutex_unlock(&mutex_fila); 
    }
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Uso correto: %s <N_Cadeiras> <Tempo_Chegada_ms> <Tempo_Atendimento_ms> <Tempo_Simulacao_s>\n", argv[0]);
        return 1;
    }

    N_cadeiras = atoi(argv[1]);
    taxa_chegada = atoi(argv[2]);
    tempo_atendimento = atoi(argv[3]);
    tempo_simulacao = atoi(argv[4]);

    fila_clientes = (int*) malloc(N_cadeiras * sizeof(int));
    
    pthread_mutex_init(&mutex_fila, NULL);
    sem_init(&sem_clientes, 0, 0); 

    gettimeofday(&tempo_inicio, NULL);

    pthread_t thread_barbeiro;
    pthread_create(&thread_barbeiro, NULL, rotina_barbeiro, NULL);

    int id_cliente_gerador = 1;
    struct timeval agora;
    unsigned int seed = time(NULL);

    while (1) {
        gettimeofday(&agora, NULL);
        if (agora.tv_sec - tempo_inicio.tv_sec >= tempo_simulacao) break;

        pthread_t t_cliente;
        int* id = malloc(sizeof(int));
        *id = id_cliente_gerador++;
        pthread_create(&t_cliente, NULL, rotina_cliente, id);
        pthread_detach(t_cliente);

        esperar_aleatorio(&seed, taxa_chegada);
    }

    simulacao_ativa = 0;
    sem_post(&sem_clientes); 
    pthread_join(thread_barbeiro, NULL);

    printf("--- FIM DO EXPEDIENTE ---\n");
    printf("Total Atendidos: %d | Total Desistentes: %d\n", clientes_atendidos, clientes_desistentes);

    pthread_mutex_destroy(&mutex_fila);
    sem_destroy(&sem_clientes);
    free(fila_clientes);

    return 0;
}