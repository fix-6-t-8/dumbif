#include "scene.h"
#include <stdio.h>

zos_err_t read10(zos_dev_t fd) {
    char buffer[11];
    uint16_t nb_octet = 10;

    zos_err_t err = read(fd, buffer, &nb_octet);
    if(err == ERR_SUCCESS) {
        buffer[nb_octet] = '\0';
        printf("Lu : %s\n", buffer);
    }

    return err;
}