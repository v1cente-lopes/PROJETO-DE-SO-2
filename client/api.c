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
    //verificar se chegou a haver connect
    if(fd_req_global == -1){
        return 1;
    }

    //enviar pedido de desconexão
    char opcode = (char) DISCONNECT;
    int num = 0;
    //escreve no fifo de pedido (request)
    if(write_all(fd_req_global, &opcode, sizeof(opcode)) != 1){
        num = 1;
    }
    
    //fechar os fifos
    close(fd_req_global);
    close(fd_resp_global);

    //apagar os fifos do cliente
    unlink(req_path_global);
    unlink(resp_path_global);

    fd_req_global = -1;
    fd_resp_global = -1;
    return num;
}

int cloudIst_define(VMType vm_type) {
    (void) vm_type;

    //verificar se já houve connect
    if(fd_req_global == -1){
        return 1;
    }

    //montar a mensagem de define, e o tamanho de bytes
    //OPCODE(1) + id(40) + input_folder(1024) + exec_path(1024) + ram(size_t) + disk(size_t) + cpu(size_t)
    char message[2113];
    
    //inicia o array a 0
    memset(message, 0, sizeof(message));
    size_t offset = 0;
    
    //o primeiro bit da mensagem, correspondente ao OPCODE
    message[offset] = (char) DEFINE;
    offset += 1;

    //bytes 1-40: o id do cliente
    memcpy(message + offset, vm_type.id, strlen(vm_type.id));
    offset += MAX_STRING_SIZE;

    //bytes 41-1065: o tamanho do input_folder
    memcpy(message + offset, vm_type.input_folder, strlen(vm_type.input_folder));
    offset += MAX_PATH_SIZE;

    //bytes 1066-2089: o tamanho do exec_path
    memcpy(message + offset, vm_type.exec_path, strlen(vm_type.exec_path));
    offset += MAX_PATH_SIZE;

    //o tamanho do campo da ram
    memcpy(message + offset, &vm_type.required.ram, sizeof(size_t));
    offset += sizeof(size_t);

    //o tamanho do campo do disk
    memcpy(message + offset, &vm_type.required.disk, sizeof(size_t));
    offset += sizeof(size_t);

    //o fim, o campo da cpu
    memcpy(message + offset, &vm_type.required.cpu, sizeof(double));

    //write_all escreve no pipe de registo do servidor, a mensagem do cliente
    if(write_all(fd_req_global, message, sizeof(message)) != 1){
        return 1;
    }

    //ler a resposta do servidor
    char result;
    if(read_all(fd_resp_global, &result, sizeof(result)) != 1){
        return 1;
    }
    //confirmar que o resultado recebido foi 0
    if(result == 0){
        return 0;
    }
    else{
        return 1;
    }
}

int cloudIst_reserve(Reservation reservation) {
    (void) reservation;

    //verificar se já houve connect
    if(fd_req_global == -1){
        return 1;
    }

    //montar a mensagem de reserve, e o tamanho de bytes
    //OPCODE(1) + id(40) + items_count(size_t) + vm_type_id(40) + amount(size_t) + server_id(size_t)
    char message[1841];
    
    //inicia o array a 0
    memset(message, 0, sizeof(message));
    size_t offset = 0;
    
    //o primeiro bit da mensagem, correspondente ao OPCODE
    message[offset] = (char) RESERVE;
    offset += 1;

    //bytes 1-40: o id da reserva
    memcpy(message + offset, reservation.id, strlen(reservation.id));
    offset += MAX_STRING_SIZE;

    //bytes 41-1065: o tamanho do items_count
    memcpy(message + offset, &reservation.items_count, sizeof(size_t));
    offset += sizeof(size_t);

    for(size_t i = 0; i < reservation.items_count; i++){
        //bytes 1066-2089: o tamanho do vm_type_id, por cada item
        memcpy(message + offset, reservation.items[i].vm_type_id, strlen(reservation.items[i].vm_type_id));
        offset += MAX_STRING_SIZE;

        //o tamanho do campo do amount
        memcpy(message + offset, &reservation.items[i].amount, sizeof(size_t));
        offset += sizeof(size_t);

        //o tamanho do campo do server_id
        memcpy(message + offset, &reservation.items[i].server_id, sizeof(size_t));
        offset += sizeof(size_t);
    }

    //write_all escreve no pipe de registo do servidor, a mensagem do cliente
    if(write_all(fd_req_global, message, offset) != 1){
        return 1;
    }

    //ler a resposta do servidor
    char result;
    if(read_all(fd_resp_global, &result, sizeof(result)) != 1){
        return 1;
    }
    //confirmar que o resultado recebido foi 0
    if(result == 0){
        return 0;
    }
    else{
        return 1;
    }
}

int cloudIst_run(char *reservation_id) {
    (void) reservation_id;

    if(strlen(reservation_id) >= MAX_STRING_SIZE){
        return 1;
    }

    //verificar se já houve connect
    if(fd_req_global == -1){
        return 1;
    }

    //montar a mensagem de run, e o tamanho de bytes
    //OPCODE(1) + id(40)
    char message[41];
    
    //inicia o array a 0
    memset(message, 0, sizeof(message));
    size_t offset = 0;
    
    //o primeiro bit da mensagem, correspondente ao OPCODE
    message[offset] = (char) EXE;
    offset += 1;

    //bytes 1-40: o id da reserva
    memcpy(message + offset, reservation_id, strlen(reservation_id));
    offset += MAX_STRING_SIZE;


    //write_all escreve no pipe de registo do servidor, a mensagem do cliente
    if(write_all(fd_req_global, message, sizeof(message)) != 1){
        return 1;
    }

    //validar o resultado obtido
    char result;
    if(read_all(fd_resp_global, &result, sizeof(result)) != 1){
        return 1;
    }
    //confirmar que o resultado recebido foi 0
    if(result == 0){
        return 0;
    }
    else{
        return 1;
    }
}

int cloudIst_list_vms() {

    //verificar se já houve connect
    if(fd_req_global == -1){
        return 1;
    }

    //montar a mensagem de pedido, que é apenas o OPCODE
    char opcode = (char) LIST;

    //write_all escreve no pipe de registo do servidor, a mensagem do cliente
    if(write_all(fd_req_global, &opcode, sizeof(opcode)) != 1){
        return 1;
    }
    
    //tratar do result, que vem da resposta do servidor
    char result;
    if(read_all(fd_resp_global, &result, sizeof(result)) != 1){
        return 1;
    }

    // ler numero de vms
    size_t num_vms;
    if(read_all(fd_resp_global, &num_vms, sizeof(num_vms)) != 1){
        return 1;
    }

    //ler a vms uma a uma, e os campos são:
    //vm_id(44), vm_type_id(40), client_id(4), server_id(size_t), vm_state(40)
    for(size_t i = 0; i < num_vms; i++){
        char entry[MAX_VM_ID_STRING + MAX_STRING_SIZE + sizeof(int) + sizeof(size_t) + 1];
        
        if(read_all(fd_resp_global, entry, sizeof(entry)) != 1) {
            return 1;
        }
        char vm_id[MAX_VM_ID_STRING];
        char vm_type_id[MAX_STRING_SIZE];
        size_t server_id;
        char vm_state;
        size_t offset = 0;

        //vm_id(44)
        memcpy(vm_id, entry + offset, MAX_VM_ID_STRING);
        //garantir que a string acaba em \0
        vm_id[MAX_VM_ID_STRING - 1] = '\0';
        offset += MAX_VM_ID_STRING;
        
        // vm_type_id(40)
        memcpy(vm_type_id, entry + offset, MAX_STRING_SIZE);
        //garantir que a string acaba em \0
        vm_type_id[MAX_STRING_SIZE - 1] = '\0';
        offset += MAX_STRING_SIZE;

        //print_vm não usa o client_id, então avançamos só
        offset += sizeof(int);

        //server_id
        memcpy(&server_id, entry + offset, sizeof(size_t));
        offset += sizeof(size_t);

        //vm_state, é 1 byte
        vm_state = entry[offset];

        //imprime cada vm da reserva em causa
        print_vm(vm_id, vm_type_id, server_id, vm_state);
    }

    //valida o resultado
    if(result == 0){
        return 0;
    }
    else{
        return 1;
    }
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