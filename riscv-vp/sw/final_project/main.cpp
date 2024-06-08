#include "string"
#include "string.h"
#include "cassert"
#include "stdio.h"
#include "stdlib.h"
#include "math.h"
#include "stdint.h"

int sem_init (uint32_t *__sem, uint32_t count) __THROW
{
  *__sem=count;
  return 0;
}

int sem_wait (uint32_t *__sem) __THROW
{
  uint32_t value, success; //RV32A
  __asm__ __volatile__("\
L%=:\n\t\
     lr.w %[value],(%[__sem])            # load reserved\n\t\
     beqz %[value],L%=                   # if zero, try again\n\t\
     addi %[value],%[value],-1           # value --\n\t\
     sc.w %[success],%[value],(%[__sem]) # store conditionally\n\t\
     bnez %[success], L%=                # if the store failed, try again\n\t\
"
    : [value] "=r"(value), [success]"=r"(success)
    : [__sem] "r"(__sem)
    : "memory");
  return 0;
}

int sem_post (uint32_t *__sem) __THROW
{
  uint32_t value, success; //RV32A
  __asm__ __volatile__("\
L%=:\n\t\
     lr.w %[value],(%[__sem])            # load reserved\n\t\
     addi %[value],%[value], 1           # value ++\n\t\
     sc.w %[success],%[value],(%[__sem]) # store conditionally\n\t\
     bnez %[success], L%=                # if the store failed, try again\n\t\
"
    : [value] "=r"(value), [success]"=r"(success)
    : [__sem] "r"(__sem)
    : "memory");
  return 0;
}


unsigned char header[54] = {
    0x42,          // identity : B
    0x4d,          // identity : M
    0,    0, 0, 0, // file size
    0,    0,       // reserved1
    0,    0,       // reserved2
    54,   0, 0, 0, // RGB data offset
    40,   0, 0, 0, // struct BITMAPINFOHEADER size
    0,    0, 0, 0, // bmp width
    0,    0, 0, 0, // bmp height
    1,    0,       // planes
    24,   0,       // bit per pixel
    0,    0, 0, 0, // compression
    0,    0, 0, 0, // data size
    0,    0, 0, 0, // h resolution
    0,    0, 0, 0, // v resolution
    0,    0, 0, 0, // used colors
    0,    0, 0, 0  // important colors
};

union word {
  int sint;
  unsigned int uint;
  unsigned char uc[4];
};

unsigned int input_rgb_raw_data_offset;
const unsigned int output_rgb_raw_data_offset=54;
int width;
int height;
unsigned int width_bytes;
unsigned char bits_per_pixel;
unsigned short bytes_per_pixel;
unsigned char *source_bitmap1;
unsigned char *target_bitmap1;
unsigned char *source_bitmap2;
unsigned char *target_bitmap2;
const int WHITE = 255;
const int BLACK = 0;
const int THRESHOLD = 90;

// Sobel Filter ACC
static char* const SOBELFILTER_START_ADDR = reinterpret_cast<char* const>(0x73000000);
static char* const SOBELFILTER_READ_ADDR  = reinterpret_cast<char* const>(0x73000004);

// Sobel Filter ACC                                                 
static char* const SOBELFILTER_START_ADDR2 = reinterpret_cast<char* const>(0x74000000);
static char* const SOBELFILTER_READ_ADDR2  = reinterpret_cast<char* const>(0x74000004);

// DMA 
static volatile uint32_t * const DMA_SRC_ADDR  = (uint32_t * const)0x70000000;
static volatile uint32_t * const DMA_DST_ADDR  = (uint32_t * const)0x70000004;
static volatile uint32_t * const DMA_LEN_ADDR  = (uint32_t * const)0x70000008;
static volatile uint32_t * const DMA_OP_ADDR   = (uint32_t * const)0x7000000C;
static volatile uint32_t * const DMA_STAT_ADDR = (uint32_t * const)0x70000010;
static const uint32_t DMA_OP_MEMCPY = 1;

bool _is_using_dma = true;//true;//false;

int read_bmp(std::string infile_name,unsigned int hart_id) {
  FILE *fp_s = NULL; // source file handler
  fp_s = fopen(infile_name.c_str(), "rb");
  if (fp_s == NULL) {
    printf("fopen %s error\n", infile_name.c_str());
    return -1;
  }
  // move offset to 10 to find rgb raw data offset
  fseek(fp_s, 10, SEEK_SET);
  assert(fread(&input_rgb_raw_data_offset, sizeof(unsigned int), 1, fp_s));

  // move offset to 18 to get width & height;
  fseek(fp_s, 18, SEEK_SET);
  assert(fread(&width, sizeof(unsigned int), 1, fp_s));
  assert(fread(&height, sizeof(unsigned int), 1, fp_s));

  // get bit per pixel
  fseek(fp_s, 28, SEEK_SET);
  assert(fread(&bits_per_pixel, sizeof(unsigned short), 1, fp_s));
  bytes_per_pixel = bits_per_pixel / 8;

  // move offset to input_rgb_raw_data_offset to get RGB raw data
  fseek(fp_s, input_rgb_raw_data_offset, SEEK_SET);
  // core_0
  if (hart_id == 0){
    source_bitmap1 =
        (unsigned char *)malloc((size_t)width * height * bytes_per_pixel);
    if (source_bitmap1 == NULL) {
      printf("malloc images_s1 error\n");
      return -1;
    }
    target_bitmap1 =
        (unsigned char *)malloc((size_t)width * height * bytes_per_pixel);
    if (target_bitmap1 == NULL) {
      printf("malloc target_bitmap1 error\n");
      return -1;
    }
  }
  // core_1
  else if (hart_id == 1){
    source_bitmap2 =
        (unsigned char *)malloc((size_t)width * height * bytes_per_pixel);
    if (source_bitmap2 == NULL) {
      printf("malloc images_s2 error\n");
      return -1;
    }
    //printf("source_bitmap2是正常的\n");
    target_bitmap2 =
      (unsigned char *)malloc((size_t)width * height * bytes_per_pixel);
    if (target_bitmap2 == NULL) {
      //printf("hard-id = %d (在read_bmp)\n", hart_id);
      printf("malloc target_bitmap2 error\n");
      return -1;
    }
  }
  if (hart_id == 0){
    assert(fread(source_bitmap1, sizeof(unsigned char),
                (size_t)(long)width * height * bytes_per_pixel, fp_s));
  }
  else if (hart_id == 1){
    assert(fread(source_bitmap2, sizeof(unsigned char),
                (size_t)(long)width * height * bytes_per_pixel, fp_s));
  }
  fclose(fp_s);
  unsigned int file_size; // file size
  // file size
  file_size = width * height * bytes_per_pixel + output_rgb_raw_data_offset;
  header[2] = (unsigned char)(file_size & 0x000000ff);
  header[3] = (file_size >> 8) & 0x000000ff;
  header[4] = (file_size >> 16) & 0x000000ff;
  header[5] = (file_size >> 24) & 0x000000ff;

  // width
  header[18] = width & 0x000000ff;
  header[19] = (width >> 8) & 0x000000ff;
  header[20] = (width >> 16) & 0x000000ff;
  header[21] = (width >> 24) & 0x000000ff;

  // height
  header[22] = height & 0x000000ff;
  header[23] = (height >> 8) & 0x000000ff;
  header[24] = (height >> 16) & 0x000000ff;
  header[25] = (height >> 24) & 0x000000ff;

  // bit per pixel
  header[28] = bits_per_pixel;

  return 0;
}

int write_bmp(std::string outfile_name, unsigned int hart_id) {
  FILE *fp_t = NULL; // target file handler

  fp_t = fopen(outfile_name.c_str(), "wb");
  if (fp_t == NULL) {
    printf("fopen %s error\n", outfile_name.c_str());
    return -1;
  }

  // write header
  fwrite(header, sizeof(unsigned char), output_rgb_raw_data_offset, fp_t);

  // write image
  if (hart_id == 0){
    fwrite(target_bitmap1, sizeof(unsigned char),
          (size_t)(long)width * height * bytes_per_pixel, fp_t);
  }
  else if (hart_id == 1){ 
    fwrite(target_bitmap2, sizeof(unsigned char),
          (size_t)(long)width * height * bytes_per_pixel, fp_t);
  }
  fclose(fp_t);
  return 0;
}

void write_data_to_ACC(char* ADDR, unsigned char* buffer, int len){
  if(_is_using_dma){  
    // Using DMA 
    *DMA_SRC_ADDR = (uint32_t)(buffer);
    *DMA_DST_ADDR = (uint32_t)(ADDR);
    *DMA_LEN_ADDR = len;
    *DMA_OP_ADDR  = DMA_OP_MEMCPY;
  }else{
    // Directly Send
    memcpy(ADDR, buffer, sizeof(unsigned char)*len);
  }
}
void read_data_from_ACC(char* ADDR, unsigned char* buffer, int len){
  if(_is_using_dma){
    // Using DMA 
    *DMA_SRC_ADDR = (uint32_t)(ADDR);
    *DMA_DST_ADDR = (uint32_t)(buffer);
    *DMA_LEN_ADDR = len;
    *DMA_OP_ADDR  = DMA_OP_MEMCPY;
  }else{
    // Directly Read
    memcpy(buffer, ADDR, sizeof(unsigned char)*len);
  }
}

#define PROCESSORS 2
uint32_t lock = 1;  
//int main(int argc, char *argv[]) {
int main(unsigned hart_id) {
  int counter = 0;
  //sem_wait(&lock);
  //printf("hard-id =* %d \n", hart_id);
  //sem_post(&lock);
    //if (hart_id == 1) printf("hart_id =! %d, counter = %d\n",hart_id, counter);
    //counter ++;
    if (hart_id == 0){
      sem_wait(&lock);
      //printf("現在hard_id =@ %d 要讀256這個\n",hart_id);
      read_bmp("lena_color_256_noise.bmp",hart_id);
      sem_post(&lock);
    }
    else if (hart_id == 1){
      sem_wait(&lock);
      //printf("現在hard_id =$ %d 要讀short這個\n",hart_id);
      read_bmp("lena_std_short_noise.bmp",hart_id); // lena_std_short_noise.bmp  lena_color_256_noise.bmp
    //read_bmp("lena_color_256_noise.bmp");
      sem_post(&lock);
    }
      sem_wait(&lock);
      printf("core %d\n",hart_id);
      printf("======================================\n");
      printf("\t  Reading from array\n");
      printf("======================================\n");
      printf(" input_rgb_raw_data_offset\t= %d\n", input_rgb_raw_data_offset);
      printf(" width\t\t\t\t= %d\n", width);
      printf(" height\t\t\t\t= %d\n", height);
      printf(" bytes_per_pixel\t\t= %d\n",bytes_per_pixel);
      printf("======================================\n");
      sem_post(&lock);

    unsigned char  buffer[4] = {0};
    unsigned char  buffer2[4] = {0};
    word data;
    int total;
    sem_wait(&lock);
    //printf("現在hard_id =$ %d\n",hart_id);
    printf("Start processing...(%d, %d)\n", width, height);
    //printf("core %d processing 之後\n", hart_id);
    sem_post(&lock);
    //for(int i = 0; i < 25; i++){ //height
      //for(int j = 0; j < 25; j++){ //width
    for(int i = 0; i < height; i++){
      for(int j = 0; j < width; j++){
        for(int v = -1; v <= 1; v++){
          for(int u = -1; u <= 1; u++){
            if((v + i) >= 0  &&  (v + i ) < width && (u + j) >= 0 && (u + j) < height ){
              if (hart_id == 0){
                buffer[0] = *(source_bitmap1 + bytes_per_pixel * ((i + v) * width + (j + u)) + 2);
                buffer[1] = *(source_bitmap1 + bytes_per_pixel * ((i + v) * width + (j + u)) + 1);
                buffer[2] = *(source_bitmap1 + bytes_per_pixel * ((i + v) * width + (j + u)) + 0);
                buffer[3] = 0;
              }
              else if (hart_id == 1){
                buffer2[0] = *(source_bitmap2 + bytes_per_pixel * ((i + v) * width + (j + u)) + 2);
                buffer2[1] = *(source_bitmap2 + bytes_per_pixel * ((i + v) * width + (j + u)) + 1);
                buffer2[2] = *(source_bitmap2 + bytes_per_pixel * ((i + v) * width + (j + u)) + 0);
                buffer2[3] = 0;
              }
            }
            else{
              buffer[0] = 0;
              buffer[1] = 0;
              buffer[2] = 0;
              buffer[3] = 0;
              buffer2[0] = 0;
              buffer2[1] = 0;
              buffer2[2] = 0;
              buffer2[3] = 0;
            }

            if (hart_id == 0){
              sem_wait(&lock);  // LOCK一開始=1表示可以進去critical section，進去後就會把lock改成0
              //printf("#core %d buffer = %d\n", hart_id, buffer[0]);
              write_data_to_ACC(SOBELFILTER_START_ADDR, buffer, 4);
              sem_post(&lock);
            }
            else if (hart_id == 1){
              sem_wait(&lock);
              //printf("#core %d buffer = %d\n", hart_id, buffer2[0]);
              write_data_to_ACC(SOBELFILTER_START_ADDR2, buffer2, 4);
              sem_post(&lock);
            }
          }
        }
      }
    }
    //for(int ii = 0; ii < 25; ii++){ //height
      //for(int jj = 0; jj < 25; jj++){ //width
    for(int ii = 0; ii < height; ii++){
      for(int jj = 0; jj < width; jj++){
        //sem_wait(&lock);
        //printf("core %d 第 %d 次\n", hart_id, 25*(ii) + (jj+1));
        //sem_post(&lock);
        if (hart_id == 0){
          sem_wait(&lock);
          read_data_from_ACC(SOBELFILTER_READ_ADDR, buffer, 4);
          sem_post(&lock);
        }
        else if (hart_id == 1){
          sem_wait(&lock);
          read_data_from_ACC(SOBELFILTER_READ_ADDR2, buffer2, 4);
          sem_post(&lock);
        }
        //memcpy(data.uc, buffer, 4);
        //total = (data).sint;
        int total2;
        if (hart_id == 0){
          sem_wait(&lock);
          total = buffer[0];
          sem_post(&lock);
        }
        else if(hart_id == 1){
          sem_wait(&lock);
          total2 = buffer2[0];
          sem_post(&lock);
        }

        if (hart_id == 0){
          //sem_wait(&lock);
          //printf("#core %d total = %d\n", hart_id, total);
          //sem_post(&lock);
          *(target_bitmap1 + bytes_per_pixel * (width * ii + jj) + 2) = total;
          *(target_bitmap1 + bytes_per_pixel * (width * ii + jj) + 1) = total;
          *(target_bitmap1 + bytes_per_pixel * (width * ii + jj) + 0) = total;
        }
        else if (hart_id == 1){
          //sem_wait(&lock);
          //printf("#core %d total = %d\n", hart_id, total2);
          //sem_post(&lock);
          *(target_bitmap2 + bytes_per_pixel * (width * ii + jj) + 2) = total2;
          *(target_bitmap2 + bytes_per_pixel * (width * ii + jj) + 1) = total2;
          *(target_bitmap2 + bytes_per_pixel * (width * ii + jj) + 0) = total2;

        }
      }
      //sem_wait(&lock);
      //printf("########## I = %d ##########\n", ii);
      //sem_post(&lock);
    }
    if (hart_id == 0){
      sem_wait(&lock);
      printf("core %d 執行end\n\n",hart_id);
      write_bmp("lena_color_256_noise_out.bmp", hart_id);
      sem_post(&lock);
    }
    else if (hart_id == 1){
      sem_wait(&lock);
      printf("core %d 執行end\n\n",hart_id);
      write_bmp("lena_std_short_noise_out.bmp", hart_id);      
      sem_post(&lock);
    }
  
}
