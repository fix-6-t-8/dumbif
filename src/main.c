#include <stdio.h>
#include <zos_errors.h>
#include <string.h>
#include <zos_vfs.h>
#include "scene.h"

int main(int argc, char** argv) {
    //Zeal8bit does not produce a standard argc, argv format.
    //argc must be an integer : 0 or 1. 1 = An argument is present.
    //argv is 1 argument only. It is passed 'as is' to the program.

    zos_dev_t fd;

    if(argc != 0) {
        if(strlen(argv[0]) > 128) {
            printf("Error: Path name must be 128 characters maximum.\n");
            return ERR_FAILURE;
        } else {
           // printf("arg = %s\n", argv[0]);
            //Open file and ...
            fd = open(argv[0], O_RDONLY);
            if(fd < 0) {
                zos_err_t err = (zos_err_t) (uint8_t) (-fd);
                printf("error while opening file : %d\n", (int) err);
                return ERR_FAILURE;
            }
            read10(fd);
        }
    }   
    else {// No path to .dat file given as argument
        printf ("usage : dumbif.bin absolute_full_path_to_the_.dat_file.\n");
        return ERR_FAILURE;
    }

    //printf("OK\n");
    close (fd);
    return ERR_SUCCESS;
}
