#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "parser.h"
#include "datacenter.h"
#include "datacenter_utils.h"
#include "constants.h"
#include "filesystem.h" // para usar o list_conf_files
#include <fcntl.h> // para poder usar o open() e as flags do open, como o O_RDONLY
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

// Mutex global para proteger o estado do datacenter
pthread_mutex_t dc_mutex = PTHREAD_MUTEX_INITIALIZER;


int main(int argc, char **argv) {
    if (argc != 8) {
        fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <host_cores> <max_clients> <reg_fifo_name>\n", argv[0]);
        return 1;
    }
    // const char *input_dir = argv[6];

    // // Valida que input_dir existe e é uma diretoria
    // if (!path_exists(input_dir)) {
    //     fprintf(stderr, "Diretoria de entrada inválida.\n");
    //     return 1;
    // }

    // static char conf_files[MAX_CONF_FILES][MAX_STRING_SIZE];
    // size_t num_conf_files;

    /*
    // Obtém a lista de ficheiros .conf ordenados alfabéticamente
    if (list_conf_files(input_dir, conf_files, &num_conf_files) != 0) {
        fprintf(stderr, "Failed to list .conf files.\n");
        return 1;
    }
    */

    //declaração das variáveis para guardar os argumentos de linha de comando 
    size_t servers;
    size_t ram;
    size_t disk;
    double cpu;
    size_t host_cores;
    size_t max_clients;

    if (parse_size_t_arg(argv[1], &servers) != 0 ||
        parse_size_t_arg(argv[2], &ram) != 0 ||
        parse_size_t_arg(argv[3], &disk) != 0 ||
        parse_double_arg(argv[4], &cpu) != 0 ||
        parse_size_t_arg(argv[5], &host_cores) != 0 ||
        parse_size_t_arg(argv[6], &max_clients) != 0 ||
        max_clients == 0 || host_cores == 0) {
        fprintf(stderr, "Invalid command line arguments.\n");
        return 1;
    }

    DataCenter dc;
    datacenter_init(&dc);

    Resources resources = {
        .ram = ram,
        .disk = disk,
        .cpu = cpu
    };

    if (datacenter_configure(&dc, servers, &resources) != 0) {
        fprintf(stderr, "Failed to configure Data Center.\n");
        datacenter_destroy(&dc);
        return 1;
    }
    
    dc.host_cores = host_cores;

    //mkfifo do de registo e damo open para a leitura, ficamo a ler do fifo ate chegar pedido de conexao
    //dps de processar o pedido atendemos o cliente, ou seja ver os fifos de peddo e resposta criado, e dps em vez de ler do de registo elmos do de pedido, criar uma funcao q processa cleine, q le do fifo de pedido para o de resposta
    
    //leitura do fifo de registo e ver se o pedido q veio do cleinte veio bem e dar parse aos fifos q veem a seguir

    //1 ficheiro por cliente ou argumento opcional


    //ler o fifo e ver se o connect veio bem, montar o fifo e processar o pedido de connect - (explorar buffer de produtor consumidor)
    //while enquanto le do buffer / fifo com read, que se bloqueia com o read até receber um pedido de connect

    //o servidor tem uma thread que processa o connect

    //primeiro e mkfifo, abrir o ficheiro q ta la dentro para letirua

    //1-conexao entre cliente e servidor
    //2- mais clientes - queues e multi sessoes - com threads para o max clientes, e semaforos para as queues

    const char *reg_fifo_name = argv[7];

    //verificação do nome do fifo
    if(strlen(reg_fifo_name) >= MAX_PIPE_PATH_LENGTH){
        fprintf(stderr, "Register FIFO name is too long.\n");
        datacenter_destroy(&dc);
        return 1;
    }

    //vamos apagar um fifo antigo com o mesmo nome
    unlink(reg_fifo_name);

    //construir o fifo
    if(mkfifo(reg_fifo_name, 0666) != 0){
        perror("mkfifo");
        datacenter_destroy(&dc);
        return 1;
    }
    
    //abre o fifo de registo
    int fd_register = open(reg_fifo_name, O_RDWR);
    //se a abertura deste fifo falhar, apagamos os anteriores
    if(fd_server == -1){
        return 1;
    }

    //fica à espera de um pedido, no loop infinito a executar o read
    while(1){
        char opcode;
        if(read_all(fd_register, &opcode, sizeof(opcode)) != 1){
            continue;
        }
        //se o opcode n for o de connect
        if(opcode != CONNECT){
            continue;
        }
        else{
            /*bloqueado*/
        }
    }
    
    
    //mais a frente: criar uma queue para atender os clientes de forma sequencial - n atender todos ao mm tempo

//#################################################################################################################################################
    // Configurar a fila para as worker threads
    // static ThreadPoolQueue queue;
    // queue.num_conf_files = num_conf_files;
    // queue.current_index = 0;
    // queue.input_dir = input_dir;
    // queue.dc = &dc;
    // queue.mutex = &dc_mutex;
    // pthread_mutex_init(&queue.queue_mutex, NULL);
    // for (size_t i = 0; i < num_conf_files; i++) {
    //     strcpy(queue.conf_files[i], conf_files[i]);
    // }

    // // Criar as worker threads com base no argumento <max_threads>
    // pthread_t *threads = malloc(max_threads * sizeof(pthread_t));
    // if (!threads) {
    //     perror("malloc threads");
    //     datacenter_destroy(&dc);
    //     return 1;
    // }

    // size_t created = 0;
    // for (size_t i = 0; i < max_threads; i++) {
    //     if (pthread_create(&threads[i], NULL, process_conf_file, &queue) != 0) {
    //         perror("pthread_create");
    //         break;
    //     }
    //     created++;
    // }

    /*
    // Aguardar que todas as worker threads concluam o processamento
    for (size_t i = 0; i < created; i++) {
        pthread_join(threads[i], NULL);
    }*/

    // Espera que as VMs ainda em execução terminem (e sejam destruídas)
    wait_for_all_vms(&dc);

    // Libertar recursos e destruir mutexes
    // free(threads);
    // pthread_mutex_destroy(&queue.queue_mutex);
    // pthread_mutex_destroy(&dc_mutex);

//#########################################################################################################

    datacenter_destroy(&dc);
    return 0;
}