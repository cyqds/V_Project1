#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  

// saving PGM data to a file
void save_pgm(const char *filename, unsigned char *data, int width, int height) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    fprintf(file, "P5\n%d %d\n255\n", width, height);
    fwrite(data, 1, width * height, file);
    fclose(file);
}

int main(int argc, char *argv[]) {
    const char *nv12_file = NULL;
    int width = 0;
    int height = 0;
    const char *output_prefix = NULL;

    // parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "i:w:h:o:")) != -1) {
        switch (opt) {
            case 'i':  
                nv12_file = optarg;
                break;
            case 'w':  
                width = atoi(optarg);
                break;
            case 'h':  
                height = atoi(optarg);
                break;
            case 'o':  
                output_prefix = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -i <nv12_file> -w <width> -h <height> -o <output_prefix>\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    // check if required arguments are provided
    if (!nv12_file || width <= 0 || height <= 0 || !output_prefix) {
        fprintf(stderr, "Missing or invalid arguments.\n");
        fprintf(stderr, "Usage: %s -i <nv12_file> -w <width> -h <height> -o <output_prefix>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // malloc memory for NV12, Y, Cb, Cr data
    unsigned char *nv12_data = (unsigned char *)malloc(width * height * 3 / 2);
    unsigned char *y_channel = (unsigned char *)malloc(width * height);
    unsigned char *cb_channel = (unsigned char *)malloc(width * height / 4);
    unsigned char *cr_channel = (unsigned char *)malloc(width * height / 4);

    if (!nv12_data || !y_channel || !cb_channel || !cr_channel) {
        perror("Failed to allocate memory");
        free(nv12_data);
        free(y_channel);
        free(cb_channel);
        free(cr_channel);
        return EXIT_FAILURE;
    }

    // read NV12 data
    FILE *file = fopen(nv12_file, "rb");
    if (!file) {
        perror("Failed to open NV12 file");
        free(nv12_data);
        free(y_channel);
        free(cb_channel);
        free(cr_channel);
        return EXIT_FAILURE;
    }
    fread(nv12_data, 1, width * height * 3 / 2, file);
    fclose(file);

    // extract Y channel
    for (int i = 0; i < width * height; i++) {
        y_channel[i] = nv12_data[i];
    }

    // extract Cb and Cr channels
    for (int i = 0; i < width * height / 4; i++) {
        cb_channel[i] = nv12_data[width * height + 2 * i];
        cr_channel[i] = nv12_data[width * height + 2 * i + 1];
    }

    // save Y, Cb, Cr channels to PGM files
    char filename[256];
    snprintf(filename, sizeof(filename), "%s_y.pgm", output_prefix);
    save_pgm(filename, y_channel, width, height);

    snprintf(filename, sizeof(filename), "%s_cb.pgm", output_prefix);
    save_pgm(filename, cb_channel, width / 2, height / 2);

    snprintf(filename, sizeof(filename), "%s_cr.pgm", output_prefix);
    save_pgm(filename, cr_channel, width / 2, height / 2);

    // free memory
    free(nv12_data);
    free(y_channel);
    free(cb_channel);
    free(cr_channel);

    printf("Y, Cb, Cr channels saved successfully.\n");
    return EXIT_SUCCESS;
}