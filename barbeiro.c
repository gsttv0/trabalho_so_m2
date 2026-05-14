#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

// Definindo os estados do Barbeiro
#define DORME 0
#define ATENDE 1

// Variáveis Globais e Parâmetros
int N_cadeiras;
int taxa_chegada;
int tempo_atendimento;
int tempo_simulacao;

// Contadores e Estruturas da Fila
int clientes_atendidos = 0;
int clientes_desistentes = 0;
int clientes_em_espera = 0;

int *fila_clientes; // Fila circular para guardar os IDs dos clientes esperando
int pos_in = 0;     // Onde o próximo cliente senta
int pos_out = 0;    // De onde o barbeiro chama o próximo cliente

int barbeiro_estado = DORME;
int id_cliente_cadeira = -1; // Quem está cortando o cabelo agora

// Sincronização
pthread_mutex_t mutex_fila; // Protege a sala de espera
sem_t sem_clientes;         // Acorda o barbeiro
sem_t sem_barbeiro;         // Libera a cadeira para o próximo

struct timeval tempo_inicio;
int simulacao_ativa = 1;

// =========================================================================
// FUNÇÕES AUXILIARES DE TEMPO E IMPRESSÃO (O Log Discreto)
// =========================================================================

void obter_tempo_atual(char *buffer) {
    struct timeval agora;
    gettimeofday(&agora, NULL);
    long segundos = agora.tv_sec - tempo_inicio.tv_sec;
    long microsegundos = agora.tv_usec - tempo_inicio.tv_usec;
    if (microsegundos < 0) { segundos -= 1; microsegundos += 1000000; }
    sprintf(buffer, "[%02ld:%02ld:%02ld.%03ld]", segundos/3600, (segundos%3600)/60, segundos%60, microsegundos/1000);
}

void esperar_aleatorio(int tempo_medio) {
    // Varia o tempo em +/- 50% do tempo médio para dar aleatoriedade
    int min = tempo_medio / 2;
    int max = tempo_medio + min;
    int tempo_ms = min + rand() % (max - min + 1);
    usleep(tempo_ms * 1000);
}

// Imprime a fotografia exata da barbearia (Sempre chamada dentro do Mutex!)
void imprimir_evento(const char* mensagem) {
    char tempo[25];
    obter_tempo_atual(tempo);

    printf("%s %s\n", tempo, mensagem);
    
    if (barbeiro_estado == DORME) printf("Barbeiro: DORME\n");
    else printf("Barbeiro: ATENDE C%d\n", id_cliente_cadeira);

    // Desenha a fila no formato exigido: [##...] (2/5) -> C1 C3
    printf("Fila: [");
    for(int i = 0; i < N_cadeiras; i++) {
        if (i < clientes_em_espera) printf("#");
        else printf(".");
    }
    printf("] (%d/%d) -> ", clientes_em_espera, N_cadeiras);
    
    // Imprime quem está na fila
    int idx = pos_out;
    for(int i = 0; i < clientes_em_espera; i++) {
        printf("C%d ", fila_clientes[idx]);
        idx = (idx + 1) % N_cadeiras; // Lógica de fila circular
    }
    printf("\n");

    printf("Contadores: atendidos = %d | desistentes = %d | em espera = %d\n\n", 
           clientes_atendidos, clientes_desistentes, clientes_em_espera);
}

// =========================================================================
// THREAD DO BARBEIRO
// =========================================================================

void* rotina_barbeiro(void* arg) {
    char msg[100];

    while (simulacao_ativa) {
        // Barbeiro dorme se não tem clientes (bloqueia no semáforo)
        sem_wait(&sem_clientes);
        
        // Se a simulação acabou e acordaram ele só para ir embora
        if (!simulacao_ativa && clientes_em_espera == 0) break;

        pthread_mutex_lock(&mutex_fila); // 🔒 ENTRA
        
        // Chama o próximo cliente da fila
        id_cliente_cadeira = fila_clientes[pos_out];
        pos_out = (pos_out + 1) % N_cadeiras;
        clientes_em_espera--;
        barbeiro_estado = ATENDE;
        
        sprintf(msg, "Barbeiro iniciou atendimento do cliente C%d", id_cliente_cadeira);
        imprimir_evento(msg);
        
        sem_post(&sem_barbeiro); // Avisa ao cliente que é a vez dele
        
        pthread_mutex_unlock(&mutex_fila); // 🔓 SAI

        // Cortando o cabelo (fora da região crítica, pois a sala de espera tá livre)
        esperar_aleatorio(tempo_atendimento);

        pthread_mutex_lock(&mutex_fila); // 🔒 ENTRA
        clientes_atendidos++;
        barbeiro_estado = DORME; // Volta a dormir (ou vai atender o próximo logo em seguida)
        
        sprintf(msg, "Barbeiro concluiu atendimento do cliente C%d", id_cliente_cadeira);
        imprimir_evento(msg);
        pthread_mutex_unlock(&mutex_fila); // 🔓 SAI
    }
    pthread_exit(NULL);
}

// =========================================================================
// THREAD DOS CLIENTES
// =========================================================================

void* rotina_cliente(void* arg) {
    int id = *((int*)arg);
    free(arg); // Libera a memória do ID
    char msg[100];

    pthread_mutex_lock(&mutex_fila); // 🔒 ENTRA na barbearia (Região Crítica)

    if (clientes_em_espera < N_cadeiras) {
        // Tem cadeira livre! Senta e espera.
        fila_clientes[pos_in] = id;
        pos_in = (pos_in + 1) % N_cadeiras;
        clientes_em_espera++;
        
        sprintf(msg, "Cliente C%d chegou e entrou na fila", id);
        imprimir_evento(msg);
        
        sem_post(&sem_clientes); // Dá um "toque" no barbeiro (acorda ele se estiver dormindo)
        pthread_mutex_unlock(&mutex_fila); // 🔓 SAI (deixa os outros entrarem)

        // Fica aguardando sua vez de sentar na cadeira do barbeiro
        sem_wait(&sem_barbeiro); 
        
    } else {
        // Barbearia lotada! Vai embora.
        clientes_desistentes++;
        sprintf(msg, "Cliente C%d chegou, mas desistiu por falta de cadeira", id);
        imprimir_evento(msg);
        pthread_mutex_unlock(&mutex_fila); // 🔓 SAI
    }

    pthread_exit(NULL);
}

// =========================================================================
// MAIN - GERADOR DE CLIENTES
// =========================================================================

int main() {
    printf("Digite: N_Cadeiras Tempo_Chegada(ms) Tempo_Atendimento(ms) Tempo_Simulacao(s)\n");
    if (scanf("%d %d %d %d", &N_cadeiras, &taxa_chegada, &tempo_atendimento, &tempo_simulacao) != 4) {
        printf("Erro na leitura.\n"); return 1;
    }

    fila_clientes = (int*) malloc(N_cadeiras * sizeof(int));
    
    pthread_mutex_init(&mutex_fila, NULL);
    sem_init(&sem_clientes, 0, 0); // Começa em 0 (Barbeiro dormindo)
    sem_init(&sem_barbeiro, 0, 0);

    gettimeofday(&tempo_inicio, NULL);
    srand(time(NULL));

    pthread_t thread_barbeiro;
    pthread_create(&thread_barbeiro, NULL, rotina_barbeiro, NULL);

    int id_cliente_gerador = 1;
    struct timeval agora;

    // O Main atua como a "Rua", gerando clientes até o tempo acabar
    while (1) {
        gettimeofday(&agora, NULL);
        if (agora.tv_sec - tempo_inicio.tv_sec >= tempo_simulacao) break;

        // Cria um novo cliente
        pthread_t t_cliente;
        int* id = malloc(sizeof(int));
        *id = id_cliente_gerador++;
        // Detach faz com que a thread limpe sua própria memória ao terminar (já que não daremos join nela)
        pthread_create(&t_cliente, NULL, rotina_cliente, id);
        pthread_detach(t_cliente);

        // Espera um tempo aleatório até o próximo cliente chegar
        esperar_aleatorio(taxa_chegada);
    }

    // Encerrando a simulação
    simulacao_ativa = 0;
    sem_post(&sem_clientes); // Acorda o barbeiro uma última vez para ele ir para casa
    pthread_join(thread_barbeiro, NULL);

    printf("--- FIM DO EXPEDIENTE ---\n");
    printf("Total Atendidos: %d | Total Desistentes: %d\n", clientes_atendidos, clientes_desistentes);

    pthread_mutex_destroy(&mutex_fila);
    sem_destroy(&sem_clientes);
    sem_destroy(&sem_barbeiro);
    free(fila_clientes);

    return 0;
}