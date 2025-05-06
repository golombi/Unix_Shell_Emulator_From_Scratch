#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define LINE_LENGTH 512

struct Command {
    struct Command* next;
    char** args;
    char* input_file;
    char* output_file;
};

//Assumes *buf is the first letter of the first word of the command
int calculateArgc(char* buf){
    int argc = 0;
    while(1){
        if((*buf==' ' || *buf=='|' || *buf=='\n' || *buf=='<' || *buf=='>') &&
            (*(buf-1)!=' ' && *(buf-1)!='|' && *(buf-1)!='\n')){
                //printf("Last char of arg '%c'\n", *(buf-1));
                argc++;
                while(*buf==' ')buf++;
        }

        while(*buf=='>' || *buf=='<'){
            buf++;
            while(*buf==' ')buf++;
            while(*buf!=' ' && *buf!='|' && *buf!='\n' && *buf!='<' && *buf!='>')buf++;
            while(*buf==' ')buf++;
        }
        if(*buf=='|' || *buf=='\n')break;
        buf++;
    }
    //printf("argc=%d\n", argc);
    return argc;
}

void printCommands(struct Command* command){
    while(command != NULL){
        int argc = 0;
        while(command->args[argc] != NULL){
            printf("'%s' (%d)\n", command->args[argc], argc);
            argc++;
        }
        if(command->input_file != NULL){
            printf("Input redirection from file '%s'\n", command->input_file);
        }
        if(command->output_file != NULL){
            printf("Output redirection to file '%s'\n", command->output_file);
        }
        command = command->next;
    }
}

char* extractWord(char* word_start, char* buf){
    char* word = malloc(buf-word_start+1);
    memcpy(word, word_start, buf-word_start);
    word[buf-word_start] = '\0';
    return word;
}

struct Command* parseCommands(char* buf) {
    struct Command* head_command = NULL;
    struct Command* command = NULL;
    while(*buf!='\n'){
        while(*buf==' ' || *buf=='|')buf++;
        if(*buf=='\n')break;
        if(command != NULL){
            command->next = malloc(sizeof(struct Command));
            command = command->next;
        }else{
            command = malloc(sizeof(struct Command));
            head_command = command;
        }
        command->input_file = NULL;
        command->output_file = NULL;
        char* word_start = buf;
        command->args = malloc((calculateArgc(buf) + 1) * sizeof(char*));
        int argc = 0;
        while(1){
            if((*buf==' ' || *buf=='|' || *buf=='\n' || *buf=='<' || *buf=='>') &&
                (*(buf-1)!=' ' && *(buf-1)!='|' && *(buf-1)!='\n')){
                    command->args[argc] = extractWord(word_start, buf);
                    //printf("'%s' (%d)\n", command->args[argc], argc);
                    argc++;
                    while(*buf==' ')buf++;
                    word_start = buf;
            }
            while(*buf=='>' || *buf=='<'){
                char** redirection_type;
                if(*buf=='>'){
                    redirection_type = &command->output_file;
                }else {
                    redirection_type = &command->input_file;
                }
                buf++;
                while(*buf==' ')buf++;
                word_start = buf;
                while(*buf!=' ' && *buf!='|' && *buf!='\n' && *buf!='<' && *buf!='>')buf++;
                *redirection_type = extractWord(word_start, buf);
                while(*buf==' ')buf++;
                word_start = buf;
            }
            if(*buf=='|' || *buf=='\n')break;
            buf++;
        }
        command->args[argc] = NULL;
    }

    if(command != NULL)command->next = NULL;
    
    //printCommands(head_command);
    return head_command;
}

//Assumes command->input_file != NULL
void setInputFile(struct Command* command){
    close(STDIN_FILENO);
    if(open(command->input_file, O_RDONLY) == -1)printf("No such file: '%s'", command->input_file);    
}

//Assumes command->output_file != NULL
void setOutputFile(struct Command* command){
    close(STDOUT_FILENO);
    open(command->output_file, O_WRONLY | O_CREAT, 0777);
}

//Assumes command != NULL
struct Command* freeAndReturnNext(struct Command* command){
    free(command->input_file);
    free(command->output_file);
    int argc = 0;
    while(command->args[argc] != NULL){
        free(command->args[argc]);
        argc++;
    }
    free(command->args);
    struct Command* next_command = command->next;
    free(command);
    return next_command;
}

int main(){
    char buf[LINE_LENGTH];
    while (1){
        //getcwd(buf, sizeof(buf));
        //printf("%s$", buf);
        fgets(buf, sizeof(buf), stdin);
        struct Command* commands = parseCommands(buf);
        if(commands != NULL){
            int pid;
            if(commands->next != NULL){
                int prv_p[2];
                pipe(prv_p);
                pid = fork();
                if(pid == 0){
                    if(commands->input_file != NULL){
                        setInputFile(commands);
                    }
                    if(commands->output_file != NULL){
                        setOutputFile(commands);
                    }else{
                        close(STDOUT_FILENO);
                        dup(prv_p[1]);
                    }
                    close(prv_p[1]);
                    close(prv_p[0]);
                    execvp(commands->args[0], commands->args);
                }
                commands = freeAndReturnNext(commands);

                int cur_p[2];
                while(commands->next != NULL){
                    pipe(cur_p);
                    close(prv_p[1]);
                    pid = fork();
                    if(pid==0){
                        if(commands->input_file != NULL){
                            setInputFile(commands);
                        }else{
                            close(STDIN_FILENO);
                            dup(prv_p[0]);
                        }
                        if(commands->output_file != NULL){
                            setOutputFile(commands);
                        }else{
                            close(STDOUT_FILENO);
                            dup(cur_p[1]);
                        }
                        close(prv_p[0]);
                        close(cur_p[0]);
                        close(cur_p[1]);
                        execvp(commands->args[0], commands->args);
                    }
                    close(prv_p[0]);
                    prv_p[0] = cur_p[0];
                    prv_p[1] = cur_p[1];
                    commands = freeAndReturnNext(commands);
                }

                close(prv_p[1]);
                pid = fork();
                if(fork() == 0){
                    if(commands->input_file != NULL){
                        setInputFile(commands);
                    }else{
                        close(STDIN_FILENO);
                        dup(prv_p[0]);
                    }
                    close(prv_p[0]);
                    if(commands->output_file != NULL){
                        setOutputFile(commands);
                    }
                    execvp(commands->args[0], commands->args);
                }
                close(prv_p[0]);   
            }else{
                pid = fork();
                if(pid==0){
                    if(commands->input_file != NULL){
                        setInputFile(commands);
                    }
                    if(commands->output_file != NULL){
                        setOutputFile(commands);
                    }
                    execvp(commands->args[0], commands->args);
                }
            }
            freeAndReturnNext(commands); 
        }
    }
    return 0;
}

