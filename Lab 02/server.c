#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // read e close
#include <arpa/inet.h>  // para sockets
#include <time.h>       // para obter o tempo atual
#include <sys/stat.h>  // para stat() que obtem info do arquivo
#include <pthread.h>   // para threads


#define BUFFER_SIZE 1024
#define SHARED_FOLDER "shared/"  // pasta dos arquivos


void *handle_client(void *arg) {
    // arg eh um ponteiro para o socket do cliente
    // tem que converter de void* para int*
    int client_socket = *(int *)arg;
    free(arg);  // liberar memória alocada no main
    char buffer[BUFFER_SIZE];
    time_t last_access = 0; // cada cliente tem seu próprio last_access

    printf("Novo cliente conectado!\n");

    while (1) {
        // encher buffer com zeros
        memset(buffer, 0, BUFFER_SIZE);
        // ler do socket do cliente
        int valread = read(client_socket, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) {
            printf("Cliente desconectado.\n");
            break;
        }
        
        buffer[strcspn(buffer, "\r\n")] = 0; // remover \n ou \r\n e colocar \0 no lugar

        // --- MyGet ---
        // strncmp compara os primeiros n caracteres de duas strings
        if (strncmp(buffer, "MyGet", 5) == 0) {
            char filename[BUFFER_SIZE];
            // %1023s limita o tamanho do nome do arquivo para evitar overflow
            sscanf(buffer + 5, "%1023s", filename);
            // se nao forneceu nome do arquivo
            // mas pode ter varios espacos
            if (buffer[5] == '\0' || strlen(filename) == 0) {
                char *msg = "ERR Nome de arquivo nao fornecido\n";
                send(client_socket, msg, strlen(msg), 0);
                continue;
            }

            char filepath[BUFFER_SIZE];
            snprintf(filepath, sizeof(filepath), "%s%s", SHARED_FOLDER, filename);
            
            FILE *fp = fopen(filepath, "rb");
            if (!fp) {
                char *msg = "ERR Arquivo nao encontrado\n";
                send(client_socket, msg, strlen(msg), 0);
                continue;
            }

            // obter tamanho do arquivo
            struct stat st;
            // stat preenche a struct st com informacoes do arquivo
            stat(filepath, &st);
            long filesize = st.st_size;


            char header[BUFFER_SIZE];
            snprintf(header, sizeof(header), "OK %ld\n", filesize);
            send(client_socket, header, strlen(header), 0);

            char filebuf[BUFFER_SIZE];
            // size_t eh um tipo sem sinal usado para representar tamanhos
            // fread retorna o numero de bytes lidos
            // o primeiro parametro eh o buffer onde os dados serao armazenados
            // o segundo eh o tamanho de cada elemento a ser lido
            // o terceiro eh o numero de elementos a serem lidos
            // o quarto eh o ponteiro para o arquivo
            // ou seja estamos lendo o arquivo em pedaços de BUFFER_SIZE bytes
            // isso eh importante para arquivos grandes que nao cabem na memoria de uma vez
            size_t n;
            while ((n = fread(filebuf, 1, BUFFER_SIZE, fp)) > 0) {
                send(client_socket, filebuf, n, 0);
            }
            fclose(fp);

            // atualizar last_access
            last_access = time(NULL);
        }
        // --- MyLastAccess ---
        else if (strncmp(buffer, "MyLastAccess", 12) == 0) {
            if (last_access == 0) {
                char *msg = "Last Access=NULL\n";
                send(client_socket, msg, strlen(msg), 0);
            } else {
                char msg[BUFFER_SIZE];
                struct tm *t = localtime(&last_access);
                strftime(msg, sizeof(msg), "Last Access=%Y-%m-%d %H:%M:%S\n", t);
                send(client_socket, msg, strlen(msg), 0);
            }
        }
        // --- Comando inválido ---
        else {
            char *msg = "ERR Comando invalido\n";
            send(client_socket, msg, strlen(msg), 0);
        }
    }

    close(client_socket);
    return NULL;
}


// argc eh o numero de argumentos da linha de comando
// argv eh um array de argumentos da linha de comando
// argv[0] eh o nome do programa
// argv[1] eh o numero da porta
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]); // atoi converte string para int
    int server_fd, new_socket; // server_fd eh o socket do servidor, new_socket eh o socket do cliente
    struct sockaddr_in address; 
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    time_t last_access = 0; // guarda ultimo acesso do cliente

    // 1. Criar socket
    // AF_INET: IPv4, SOCK_STREAM: TCP
    // AF_INET e SOCK_STREAM sao constantes definidas em <arpa/inet.h> e <sys/socket.h>
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }
    // server_fd agora eh o socket do servidor

    // 2. Configurar endereço
    address.sin_family = AF_INET; // Estamos usando IPv4, isso significa que o endereco sera do tipo IPv4, que eh o formato mais comum
    address.sin_addr.s_addr = INADDR_ANY; // INADDR_ANY significa que o servidor vai aceitar conexoes de qualquer endereco IP
    address.sin_port = htons(port); // htons converte para big-endian, isso eh necessario para a rede, pois a rede usa big-endian

    // Na pratica, o que foi feito a cima foi configurar o endereco do servidor
    // para aceitar conexoes de qualquer endereco IP na porta especificada com o protocolo IPv4

    // 3. Bind
    // bind serve para associar o socket a um endereco e porta
    // a diferenca entre bind, socket, endereco e porta eh que:
    // - socket eh o ponto de comunicacao, ele eh como um canal
    // - endereco eh o IP do computador
    // - porta eh como uma "porta" de entrada para o canal, cada aplicacao usa uma porta diferente
    // - bind associa o socket ao endereco e porta, ou seja, ele "liga" o canal a um endereco e porta especificos
    
    //Estamos criando um novo ponteiro em vez de usar o endereco diretamente, pois a funcao bind espera um ponteiro do tipo struct sockaddr e nao do tipo struct sockaddr_in, a diferencia eh que struct sockaddr eh uma estrutura generica que pode representar qualquer tipo de endereco, enquanto struct sockaddr_in eh especifica para enderecos IPv4
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Erro no bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 4. Escuta
    // listen faz o socket "ouvir" por conexoes
    // o segundo parametro (3) eh o tamanho da fila de conexoes pendentes
    // ou seja, quantas conexoes podem ficar esperando para serem aceitas
    // se a fila estiver cheia, novas conexoes serao rejeitadas
    if (listen(server_fd, 3) < 0) {
        perror("Erro no listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor rodando na porta %d...\n", port);

    // 5. Loop aceitando clientes
    while (1) {
        int *new_socket = malloc(sizeof(int));
        if ((*new_socket = accept(server_fd, (struct sockaddr *)&address,
                        (socklen_t*)&addrlen)) < 0) {
            perror("Erro no accept");
            free(new_socket);
            continue; // não derruba o servidor
        }

        // criar uma thread para cada cliente
        // os parametros da thread sao:
        // 1. ponteiro para a variavel que vai guardar o ID da thread
        // 2. atributos da thread (NULL = default)
        // 3. funcao que a thread vai executar
        // 4. argumento para a funcao (o socket do cliente) 
        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, new_socket) != 0) {
            perror("Erro ao criar thread");
            close(*new_socket);
            free(new_socket);
        } else {
            pthread_detach(tid); // thread independente
        }
    }

    // 7. Fechar conexoes
    close(new_socket);
    close(server_fd);

    return 0;
}