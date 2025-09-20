#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <arpa/inet.h>  // para sockets
#include <libgen.h>  // basename


#define BUFFER_SIZE 1024

// argc is the number of command line arguments
// argv is an array of command line arguments
// argv[0] is the program name
// argv[1] is the server IP address
// argv[2] is the server port number
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Uso: %s <ip_servidor> <porta>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // string com o IP do servidor
    char *ip = argv[1];
    // converter porta de string para int
    int port = atoi(argv[2]);
    // socket_client eh o socket do cliente
    int socket_client = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    // 1. Criar socket
    // AF_INET: IPv4, SOCK_STREAM: TCP
    // AF_INET e SOCK_STREAM sao constantes definidas em <arpa/inet.h> e <sys/socket.h>
    if ((socket_client = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Erro ao criar socket");
        exit(EXIT_FAILURE);
    }

    // 2. Configurar endereco

    serv_addr.sin_family = AF_INET; // Estamos usando IPv4, isso significa que o endereco sera do tipo IPv4, que eh o formato mais comum
    serv_addr.sin_port = htons(port); // htons converte para big-endian, isso eh necessario para a rede, pois a rede usa big-endian


    // inet_pton converte o endereco IP de texto para binario e retorna 1 se sucesso, retorna 0 se endereco invalido, retorna -1 se erro
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        perror("Endereco invalido ou nao suportado");
        exit(EXIT_FAILURE);
    }

    // 3. Conectar
    // connect conecta o socket do cliente ao endereco do servidor
    // o primeiro parametro eh o socket do cliente
    // o segundo parametro eh um ponteiro para a struct sockaddr que contem o endereco do servidor
    // o terceiro parametro eh o tamanho da struct sockaddr
    // retorn 0 se sucesso, -1 se erro
    if (connect(socket_client, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Falha na conexao");
        exit(EXIT_FAILURE);
    }

    printf("Conectado ao servidor %s:%d\n", ip, port);
    printf("Digite comandos (MyGet <arquivo> ou MyLastAccess):\n");

    // 4. Loop de interacao
    while (1) {
        printf("> ");
        // fflush eh uma funcao que limpa o buffer de saida
        // fflush(stdout);

        // ler comando do usuario
        memset(buffer, 0, BUFFER_SIZE);
        if (!fgets(buffer, BUFFER_SIZE, stdin)) break;

        char usercmd[BUFFER_SIZE];
        strcpy(usercmd, buffer);

        // enviar comando
        send(socket_client, buffer, strlen(buffer), 0);

        // receber resposta
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(socket_client, buffer, BUFFER_SIZE - 1);
        if (valread <= 0) {
            printf("Servidor desconectou.\n");
            break;
        }

        buffer[valread] = '\0';

        // se for resposta de arquivo (OK <tamanho>)
        if (strncmp(buffer, "OK", 2) == 0) {
            long filesize;
            sscanf(buffer, "OK %ld", &filesize);

            // extrair nome do arquivo do comando digitado
            // char comando[BUFFER_SIZE], filename[BUFFER_SIZE];
            // sscanf(buffer, "%s %s", comando, filename); 

            // localizar fim do cabeçalho
            char *filedata = strchr(buffer, '\n');
            
            // se encontrou o \n, entao o conteudo do arquivo comeca depois dele
            long bytes_already_read = 0;
            if (filedata) {
                filedata++; // aponta pro início do conteúdo
                bytes_already_read = valread - (filedata - buffer);
            }

            // abrir arquivo para escrita
            char cmd[BUFFER_SIZE], fname[BUFFER_SIZE];
            FILE *out = NULL;

            // pegar nome do arquivo digitado
            if (sscanf(usercmd, "%s %s", cmd, fname) == 2) {
                
                char *justname = basename(fname);
                out = fopen(justname, "wb");
                // out = fopen(fname, "wb");
                if (!out) {
                    perror("Erro ao criar arquivo");
                } else {
                    printf("Baixando arquivo: %s (%ld bytes)\n", fname, filesize);
                }
            }

            // se conseguiu abrir o arquivo
            if (out) {
                // escrever o que já veio junto com header
                if (bytes_already_read > 0) {
                    fwrite(filedata, 1, bytes_already_read, out);
                }

                // ler o restante
                long remaining = filesize - bytes_already_read;
                while (remaining > 0) {
                    valread = read(socket_client, buffer, BUFFER_SIZE);
                    if (valread <= 0)
                        break;
                    fwrite(buffer, 1, valread, out);
                    remaining -= valread;
                }
                fclose(out);
                printf("Download concluído!\n");
            }

        } else {
            // resposta normal (MyLastAccess, erros, etc.)
            printf("%s\n", buffer);
        }

    }

    close(socket_client);
    return 0;
}
