#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

#include "api.h"
#include "parser.h"
#include "constants.h" //para usar MAX_PIPE_PATH_LENGTH
#include "filesystem.h" //para usar o write_all

void pretty_print_cloud_specs(size_t num_servers, size_t ram, size_t disk, double cpus) {
    printf("╔══════════════════════════════════════════╗\n");
    printf("║         CloudIST - Connected!            ║\n");
    printf("╠══════════════════════════════════════════╣\n");
    printf("║                                          ║\n");
    printf("║  Servers       : %-24zu║\n", num_servers);
    printf("║  RAM / Server  : %-24zu║\n", ram);
    printf("║  Disk / Server : %-24zu║\n", disk);
    printf("║  CPUs / Server : %-24.2f║\n", cpus);
    printf("║                                          ║\n");
    printf("╚══════════════════════════════════════════╝\n");
    printf("\n");
}

char *convert_vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        case VM_STATE_TERMINATED: return "TERMINATED";
        default:                  return "UNKNOWN";
    }
}

void print_vm(char * vm_id, char * vm_type_id, size_t server_id, char vm_state) {
    printf("VM ID : %s | VM Type: %s | Server ID: %zu | State: %s\n",
                vm_id,
                vm_type_id,
                server_id,
                convert_vm_state_to_string((VMState) vm_state));
}

//variáveis globais a ser usadas pelos comandos
static int fd_req_global = -1;
static int fd_resp_global = -1;
static char req_path_global[MAX_PIPE_PATH_LENGTH];
static char resp_path_global[MAX_PIPE_PATH_LENGTH];


int cloudIst_connect(char const* req_pipe_path, char const* resp_pipe_path, char const* server_pipe_path, unsigned int client_id) {
    
    (void) req_pipe_path;
    (void) resp_pipe_path;
    (void) server_pipe_path;
    (void) client_id;

    //validar os caminhos - têm de caber em 40 bytes, ou seja, têm de ser
    //menores que MAX_PIPE_PATH_LENGTH
    if(strlen(req_pipe_path) >= MAX_PIPE_PATH_LENGTH ||
        strlen(resp_pipe_path) >= MAX_PIPE_PATH_LENGTH){
        return 1;
    }

    //apagar fifos antigos caso existam
    unlink(resp_pipe_path);
    unlink(req_pipe_path);
    
    //criação dos fifos com os nomes dos 1º e 2º argumentos
    if (mkfifo(req_pipe_path, 0666) < 0){  
        return 1;
    }

    if (mkfifo(resp_pipe_path, 0666) < 0){
        //apaga o fifo criado antes, caso este falhe
        unlink(req_pipe_path); 
        return 1;
    }

    //abrir o fifo de registo do servidor, para escrever
    int fd_server = open(server_pipe_path, O_WRONLY);
    //se a abertura deste fifo falhar, apagamos os anteriores
    if(fd_server == -1){
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    //montar a mensagem de pedido de conexão:
    //campos: 1 byte opcode + 40(req_pipe) + 40(resp_pipe) + 4(byte de id de cliente)
    char message[85];
    
    //inicia o array a 0
    memset(message, 0, sizeof(message));
    size_t offset = 0;
    
    //o primeiro bit da mensagem, correspondente ao OPCODE
    message[offset] = (char) CONNECT;
    offset += 1;

    //bytes 1-40: o caminho do FIFO de pedidos
    memcpy(message + offset, req_pipe_path, strlen(req_pipe_path));
    offset += MAX_PIPE_PATH_LENGTH;

    //bytes 41-80: o caminho do FIFO de respostas
    memcpy(message + offset, resp_pipe_path, strlen(resp_pipe_path));
    offset += MAX_PIPE_PATH_LENGTH;

    //bytes 81-84: o id do cliente, client_id
    memcpy(message + offset, &client_id, sizeof(client_id));

    //write_all escreve no pipe de registo do servidor, a mensagem do cliente
    if(write_all(fd_server, message, sizeof(message)) != 1){
        //fechar o fifo de registo do servidor, caso dê erro
        close(fd_server);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }
    //fecha o fifo de registo do servidor
    close(fd_server);

    //começar por abrir o fifo de pedidos do cliente
    int client_req_fd = open(req_pipe_path, O_WRONLY);
    if(client_req_fd == -1){
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    //abrir o fifo de respostas do servidor
    int server_resp_fd = open(resp_pipe_path, O_RDONLY);
    if(server_resp_fd == -1){
        //fecha o fifo do cliente, caso o do servidor falhe a abrir
        close(client_req_fd);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }
    
    //a mensagem recebida do servidor é:
    //result + num_servers + ram + disk + cpus
    char response[1 + 3 * sizeof(size_t) + sizeof(double)];
    if(read_all(server_resp_fd, response, sizeof(response)) != 1){
        close(client_req_fd);
        close(server_resp_fd);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }

    size_t num_servers = 0, ram = 0, disk = 0;
    double cpus = 0.0;

    //cópia de cada campo da resposta, para a variável correspondente
    size_t offset_resp = 0;
    char result = response[offset_resp];
    //se result não for 0, a operação não foi bem sucedida
    if(result != 0){
        close(client_req_fd);
        close(server_resp_fd);
        unlink(req_pipe_path);
        unlink(resp_pipe_path);
        return 1;
    }
    offset_resp += 1;

    memcpy(&num_servers, response + offset_resp, sizeof(size_t));
    offset_resp += sizeof(size_t);

    memcpy(&ram, response + offset_resp, sizeof(size_t));
    offset_resp += sizeof(size_t);

    memcpy(&disk, response + offset_resp, sizeof(size_t));
    offset_resp += sizeof(size_t);

    memcpy(&cpus, response + offset_resp, sizeof(double));

    //atualizar as variáveis globais, guardamos o estado delas após o connect
    fd_req_global  = client_req_fd;
    fd_resp_global = server_resp_fd;
    strcpy(req_path_global,  req_pipe_path);
    strcpy(resp_path_global, resp_pipe_path);

    //TODO: create pipes and connect to the server through the server_pipe_path

    //printf("Connect: Implement me pls\n");

    //Print server's specifications to stdout
    pretty_print_cloud_specs(num_servers, ram, disk, cpus);
    return 0; 
}


int cloudIst_disconnect(void){
    //TODO close pipes
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_define(VMType vm_type) {
    (void) vm_type;
    //TODO: send define command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_reserve(Reservation reservation) {
    (void) reservation;
    //TODO: send reserve command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_run(char *reservation_id) {
    (void) reservation_id;
    //TODO: send run/arranca command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");
    return 0;
}

int cloudIst_list_vms() {
    //TODO: send List command to the server (through the request pipe) and wait for the response (through the response pipe)
    printf("Implement me pls\n");

    //TODO: dont forget to print output to stdout using the format from function print_vm()
    return 0;
}


int send_command(ParsedCommand *cmd) {

    switch (cmd->type) {
        case CMD_DEFINE:
            if (cloudIst_define(cmd->data.vmtype)) {
                fprintf(stderr, "Error defining VM type %s\n", cmd->data.vmtype.id);
            } else {
                fprintf(stdout, "Defined VM %s\n", cmd->data.vmtype.id);
            }
            break;

        case CMD_RESERVE:
            if (cloudIst_reserve(cmd->data.reservation)) {
                fprintf(stderr, "Error making reservation %s\n", cmd->data.reservation.id);
            } else {
                fprintf(stdout, "Reserved %s\n", cmd->data.reservation.id);
            }
            break;

        case CMD_EXECUTE:
            if (cloudIst_run(cmd->data.id)) {
                fprintf(stderr, "Failed to execute reservation %s.\n", cmd->data.id);
            } else {
                printf("Running reservation %s\n", cmd->data.id);
            }
            break;

        case CMD_LIST:
            if (cloudIst_list_vms()) {
                fprintf(stderr, "Failed to list VMs.\n");
            } else {
                printf("show list");
            }
            break;

        case CMD_WAIT:
            if (cmd->data.delay > 0) {
                printf("Waiting...\n");
                struct timespec delay = {
                    .tv_sec = (time_t)(cmd->data.delay / 1000U),
                    .tv_nsec = (long)(cmd->data.delay % 1000U) * 1000000L
                };
                nanosleep(&delay, NULL);
            }
            break;
            
        case CMD_EMPTY:
            break;

        case CMD_HELP: 
            break;

        case CMD_INVALID:
            return 1;

        case EOC:
            return 1;

    }

    return 0;
}