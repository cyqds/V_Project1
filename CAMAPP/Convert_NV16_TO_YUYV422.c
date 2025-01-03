#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>



void nv16_to_yuyv422(const unsigned char* nv16, unsigned char* yuyv422, int width, int height) {
    int y_size = width * height;
    const unsigned char* y_plane = nv16;          
    const unsigned char* uv_plane = nv16 + y_size; 

    for (int i = 0, j = 0; i < y_size; i += 2, j += 4) {
        // read Y components
        unsigned char y0 = y_plane[i];
        unsigned char y1 = y_plane[i + 1];

        // read UV components
        unsigned char u = uv_plane[i];
        unsigned char v = uv_plane[i + 1];

        // write YUYV422 data
        yuyv422[j] = y0;      // Y0
        yuyv422[j + 1] = u;   // U
        yuyv422[j + 2] = y1;  // Y1
        yuyv422[j + 3] = v;   // V
    }
// for (int y = 0; y < height; y++) {
//         for (int x = 0; x < width; x += 2) {
//             // extract Y components
//             int y_index = y * width + x;
//             unsigned char y0 = nv16_data[y_index];          
//             unsigned char y1 = nv16_data[y_index + 1];      

//             // extract UV components
//             int uv_index = width * height + y * width + x;  
//             unsigned char u = nv16_data[uv_index];          
//             unsigned char v = nv16_data[uv_index + 1];    

//             // 
//             int yuyv_index = y * width * 2 + x * 2;        
//             yuyv422_data[yuyv_index] = y0;                  
//             yuyv422_data[yuyv_index + 1] = u;               
//             yuyv422_data[yuyv_index + 2] = y1;             
//             yuyv422_data[yuyv_index + 3] = v;               
//         }
}
    


// saving YUYV422 data to a file
void save_yuyv422(const char *filename, unsigned char *data, int width, int height) {
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    fwrite(data, 1, width * height * 2, file); 
    fclose(file);
}

int main(int argc, char *argv[]) {
    const char *nv16_file = NULL;
    int width = 0;
    int height = 0;
    const char *output_file = NULL;

    // parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "i:w:h:o:")) != -1) {
        switch (opt) {
            case 'i':  
                nv16_file = optarg;
                break;
            case 'w':  
                width = atoi(optarg);
                break;
            case 'h':  
                height = atoi(optarg);
                break;
            case 'o':  
                output_file = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s -i <nv16_file> -w <width> -h <height> -o <output_file>\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    if (width <= 0 || height <= 0 ) {
        fprintf(stderr, "Invalid width or height\n");
        return EXIT_FAILURE;
    }

    // malloc memory for NV16 and YUYV422 data
    unsigned char *nv16_data = malloc(width * height * 2);
    unsigned char *yuyv422_data = malloc(width * height * 2); height;
    if (!nv16_data || !yuyv422_data) {
        perror("Failed to allocate memory");
        free(nv16_data);
        free(yuyv422_data);
        return EXIT_FAILURE;
    }

    // read NV16 data
    FILE *file = fopen(nv16_file, "rb");
    if (!file) {
        perror("Failed to open NV16 file");
        free(nv16_data);
        free(yuyv422_data);
        return EXIT_FAILURE;
    }
    fread(nv16_data, 1, width * height * 2, file);
    fclose(file);

    // convert NV16 to YUYV422
    nv16_to_yuyv422(nv16_data, yuyv422_data, width, height);

    // save YUYV422 data to a file
    save_yuyv422(output_file, yuyv422_data, width, height);

    // free memory
    free(nv16_data);
    free(yuyv422_data);

    printf("NV16 to YUYV422 conversion completed. Output saved to %s\n", output_file);
    return EXIT_SUCCESS;
}
