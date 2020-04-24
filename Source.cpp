#include <stdio.h>
#include <stdlib.h> 
#include <string>
#include <malloc.h>
#include <iostream>
#include <fstream>
#include <omp.h>
#include "mpi.h"

extern "C" {
	#include "aes.h"
}
using namespace std;

//  Options:
// -e aes_key.txt  message.txt messageOut.txt
// -d aes_key.txt  messageOut.txt messageInitial.txt

char* readAesKeyFromFile(char** argv) {
	fstream keyFile;
	keyFile.open(argv[2], ios::in);
	char* aes_key = (char*)malloc(16);

	if (keyFile.is_open()) {
		string key;
		getline(keyFile, key);
		strcpy(aes_key, key.c_str());
	}
	return aes_key;
}

char* encrypt_file_sequential(char** argv, char* symmetricKey) {
	printf("Encypting %s ...\n", argv[3]);
	FILE *inputFile = fopen(argv[3], "rb");
	FILE *outputFile = fopen(argv[4], "wb");
	long inLen;

	struct AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t *) symmetricKey);

	fseek(inputFile, 0L, SEEK_END);
	inLen = ftell(inputFile);
	rewind(inputFile);

	long int outLen = 0;
	if ((inLen % 16) == 0)
		outLen = inLen;
	else
		outLen = ((inLen / 16) * 16) + 16;

	char *buffer = (char*)calloc(1, inLen + 1);
	if (!buffer) fclose(inputFile), fputs("memory alloc fails", stderr), exit(1);

	if (fread(buffer, inLen, 1, inputFile) != 1)
		fclose(inputFile), free(buffer), fputs("entire read fails", stderr), exit(1);

	for (int i = 0; i < (outLen / 16); i++) {
		AES_ECB_encrypt(&ctx, (uint8_t*)buffer + (i * 16));
	}

	fwrite(&inLen, sizeof(inLen), 1, outputFile);
	fwrite(buffer, outLen, 1, outputFile);
	fclose(inputFile);
	fclose(outputFile);
	printf("Encryption finished. Check file %s\n" , argv[4]);
	return buffer;
}

void encrypt_file_parallel(char** argv, int argc, char* symmetricKey) {
	
	FILE *inputFile = fopen(argv[3], "rb");
	FILE *outputFile = fopen(argv[4], "wb");
	long inLen;

	struct AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t *)symmetricKey);

	fseek(inputFile, 0L, SEEK_END);
	inLen = ftell(inputFile);
	rewind(inputFile);

	long int outLen = 0;
	if ((inLen % 16) == 0)
		outLen = inLen;
	else
		outLen = ((inLen / 16) * 16) + 16;

	int size, rank;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	char *globalBuffer = NULL;
	const long int chunkSize = outLen / size;
	char* localBuffer = (char*)malloc(chunkSize);

	if (rank == 0) {
		globalBuffer = (char*)malloc(inLen + 1);
  		if (!globalBuffer) fclose(inputFile), fputs("memory alloc fails", stderr), exit(1);

		if (fread(globalBuffer, inLen, 1, inputFile) != 1)
			fclose(inputFile), free(globalBuffer), fputs("entire read fails", stderr), exit(1);

		printf("Processor %d has read the input file\n", rank);
		printf("Encypting %s ...\n", argv[3]);
	}

	MPI_Scatter(globalBuffer, chunkSize, MPI_CHAR, localBuffer, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);


 	printf("Processor %d received data.\n", rank);

#pragma omp parallel for schedule(static, 2)
	for (int i = 0; i < (outLen / 16); i++) {
		AES_ECB_encrypt(&ctx, (uint8_t*)localBuffer + (i * 16));
	}

	printf("Processor %d encrypted the data.\n", rank);

	MPI_Gather(localBuffer, chunkSize, MPI_CHAR, globalBuffer, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);

	if (rank == 0) {
		fwrite(&inLen, sizeof(inLen), 1, outputFile);
		fwrite(globalBuffer, outLen, 1, outputFile);
		fclose(inputFile);
		fclose(outputFile); 
		printf("Encryption finished. Check file %s\n", argv[4]);
	}
	MPI_Finalize();
}

char* decrypt_file_sequential(char** argv, char* symmetricKey) {
	printf("Decrypting %s ...\n", argv[3]);
	FILE *inputFile = fopen(argv[3], "rb");
	FILE *outputFile = fopen(argv[4], "wb");
	long inLen;

	struct AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t *)symmetricKey);

	fseek(inputFile, 0, SEEK_END);
	inLen = ftell(inputFile) - 4;
	fseek(inputFile, 0, SEEK_SET);
	long int outLen = 0;
	fread(&outLen, sizeof(outLen), 1, inputFile);

	char *buffer = (char*)calloc(1, inLen + 1);
	memset(buffer, 0x00, inLen);
	fread(buffer, inLen, 1, inputFile);

	for (int i = 0; i < (inLen / 16); i++) {
		AES_ECB_decrypt(&ctx, (uint8_t*)buffer + (i * 16));
	}

	fwrite(buffer, outLen, 1, outputFile);
	fclose(inputFile);
	fclose(outputFile);

	printf("Decryption finished. Check file %s\n", argv[4]);
	return buffer;
}

void decrypt_file_parallel(char** argv, int argc, char* symmetricKey) {
	
	FILE *inputFile = fopen(argv[3], "rb");
	FILE *outputFile = fopen(argv[4], "wb");
	long inLen;

	struct AES_ctx ctx;
	AES_init_ctx(&ctx, (uint8_t *)symmetricKey);

	fseek(inputFile, 0, SEEK_END);
	inLen = ftell(inputFile) - 4;
	fseek(inputFile, 0, SEEK_SET);
	long int outLen = 0;
	fread(&outLen, sizeof(outLen), 1, inputFile);
	char *globalBuffer;


	int size, rank;
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	const long int chunkSize = inLen / size;
	char* localBuffer = (char*)malloc(chunkSize);

	if (rank == 0) {
		globalBuffer =(char*)malloc(inLen + 1);
		if (!globalBuffer) fclose(inputFile), fputs("memory alloc fails", stderr), exit(1);

		fread(globalBuffer, inLen, 1, inputFile);

		printf("Processor %d has read the input file\n", rank);
		printf("Decrypting %s ...\n", argv[3]);
	}

	MPI_Scatter(globalBuffer, chunkSize, MPI_CHAR, localBuffer, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);

	printf("Processor %d received data.\n", rank);

#pragma omp parallel for schedule(static, 5)
	for (int i = 0; i < (inLen / 16); i++) {
		AES_ECB_decrypt(&ctx, (uint8_t*)localBuffer + (i * 16));
	}

	printf("Processor %d decrypted the data.\n", rank);

	MPI_Gather(localBuffer, chunkSize, MPI_CHAR, globalBuffer, chunkSize, MPI_CHAR, 0, MPI_COMM_WORLD);

	MPI_Finalize();

	if (rank == 0) {
		fwrite(globalBuffer, outLen, 1, outputFile);
		fclose(inputFile);
		fclose(outputFile);
		printf("Decryption finished. Check file %s\n", argv[4]);
	}
}

int main(int argc, char**argv) {
	if (argc == 5) {
		
		char* aesKey = readAesKeyFromFile(argv);
		
		if (strcmp(argv[1], "-e") == 0) {
			

			//benchmark for sequential solution
			long tStart = omp_get_wtime();
			//encrypt_file_sequential(argv, aesKey);
			long tFinal = omp_get_wtime();
			long duration = (tFinal - tStart);

			//printf("\nDuration of encyrption (sequential): %ld s\n", duration);
			//printf("\n****************************************\n");

			//benchmark for parallel solution
			tStart = omp_get_wtime();
			encrypt_file_parallel(argv, argc, aesKey);
			tFinal = omp_get_wtime();
			duration = (tFinal - tStart);

			printf("\nDuration of encyrption (parallel): %ld s\n", duration);

		} else {

			//benchmark for sequential solution
			long tStart = omp_get_wtime();
			//decrypt_file_sequential(argv, aesKey);
			long tFinal = omp_get_wtime();
			long duration = (tFinal - tStart);

			//printf("\nDuration of decyrption (sequential): %ld s\n", duration);
			//printf("\n****************************************\n");

			//benchmark for sequential solution
			tStart = omp_get_wtime();
			decrypt_file_parallel(argv, argc, aesKey);
			tFinal = omp_get_wtime();
			duration = (tFinal - tStart);

			printf("\nDuration of decyrption (parallel): %ld s\n", duration);

		}
	}
	else {
		printf("\nInvalid number of arguments. Usage Mode : AES_Parallel_Proj.exe -e aes_key fSrc.txt fDst.txt");
		printf("\nInvalid number of arguments. Usage Mode : AES_Parallel_Proj.exe -d aes_key fSrc.txt fDst.txt");
	}

	return 0;
}