#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>

typedef struct
{
    char path_files[256];
    char inventory_file[256];
    char log_file[256];
    int num_procesos;
    int simulate_sleep;
    char *ruta_procesados;
    sem_t mutex_log;
    sem_t file_semaphore;
} AppConfig;

typedef struct
{
    char idOp[6];
    char fechaIni[20];
    char fechaFin[20];
    char idUsu[6];
    char tipoOpe[8];
    int numOp;
    float importe;
    char estado[12];
} infoSucur;

typedef struct
{
    AppConfig *config;
    infoSucur *sucursal;
    int *index;
} ProcesarFicheroArgs;

// Prototipos de funciones para que puedan ser reconocidos antes de su implementación
void escribirLog(AppConfig *config, char *cadena);
void ProcesarFichero(AppConfig *config, infoSucur *sucursal, int *index);
int readConfig(const char *filename, AppConfig *config);
void *leerArchivo(void *arg);
void initSemaphores(AppConfig *config);
int contarLineas(const char *nombreArchivo);

int main()
{
    AppConfig config;
    config.ruta_procesados=  "./archivosProcesados/";
    initSemaphores(&config);

    if (readConfig("./fp.conf", &config) != 0)
    {
        fprintf(stderr, "Error al leer el archivo de configuración.\n");
        return EXIT_FAILURE;
    }

    escribirLog(&config, "Inicio del programa.");

    DIR *dir = opendir(config.path_files);
    if (dir == NULL)
    {
        escribirLog(&config, "Error al abrir el directorio:");
        return EXIT_FAILURE;
    }

    struct dirent *entry;
    int numLineasTotal = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            char filePath[512];
            sprintf(filePath, "%s%s", config.path_files, entry->d_name);
            int lineas = contarLineas(filePath);
            if (lineas < 0)
            {
                closedir(dir);
                escribirLog(&config, "Error al contar las líneas del archivo:");
                return EXIT_FAILURE;
            }
            numLineasTotal += lineas;
        }
    }
    closedir(dir);

    infoSucur *sucursal = malloc(numLineasTotal * sizeof(infoSucur));
    if (!sucursal)
    {
        escribirLog(&config, "Error al alocar memoria para 'sucursal'.");
        return EXIT_FAILURE;
    }

    int index = 0;
    ProcesarFicheroArgs args = {&config, sucursal, &index};

    pthread_t hilos[config.num_procesos];
    for (int i = 0; i < config.num_procesos; i++)
    {
        if (pthread_create(&hilos[i], NULL, leerArchivo, (void *)&args))
        {
            escribirLog(&config, "Error al crear el hilo.");
            // Manejar error y posiblemente terminar la ejecución
        }
    }

    // Esperar a que todos los hilos terminen
    for (int i = 0; i < config.num_procesos; i++)
    {
        if (pthread_join(hilos[i], NULL))
        {
            escribirLog(&config, "Error al unirse al hilo.");
            // Manejar error y posiblemente terminar la ejecución
        }
    }

    free(sucursal);

    escribirLog(&config, "Fin del programa.");
    sem_destroy(&config.mutex_log);
    sem_destroy(&config.file_semaphore);

    return 0;
}

int readConfig(const char *filename, AppConfig *config)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error al abrir el archivo de configuración");
        return -1; // Error al abrir el archivo
    }

    char line[512];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");
        char *endptr;

        if (strcmp(key, "PATH_FILES") == 0)
        {
            strncpy(config->path_files, value, sizeof(config->path_files) - 1);
            config->path_files[strcspn(config->path_files, "\r")] = '\0'; // Ensure null-termination
        }
        else if (strcmp(key, "INVENTORY_FILE") == 0)
        {
            strncpy(config->inventory_file, value, sizeof(config->inventory_file) - 1);
            config->inventory_file[strcspn(config->inventory_file, "\r")] = '\0'; // Ensure null-termination
        }
        else if (strcmp(key, "LOG_FILE") == 0)
        {
            strncpy(config->log_file, value, sizeof(config->log_file) - 1);
            config->log_file[strcspn(config->log_file, "\r")] = '\0'; // Ensure null-termination
        }
        else if (strcmp(key, "NUM_PROCESOS") == 0)
        {
            config->num_procesos = (int)strtol(value, &endptr, 10);
        }
        else if (strcmp(key, "SIMULATE_SLEEP") == 0)
        {
            config->simulate_sleep = (int)strtol(value, &endptr, 10);
        }
        // Ignore any line that does not match the expected keys
    }

    fclose(file);

    // Initialize semaphores here after successful configuration
    sem_init(&config->mutex_log, 0, 1);
    sem_init(&config->file_semaphore, 0, config->num_procesos);

    return 0; // Success
}

void *leerArchivo(void *arg)
{
    ProcesarFicheroArgs *args = (ProcesarFicheroArgs *)arg;
    ProcesarFichero(args->config, args->sucursal, args->index);
    pthread_exit(NULL);
}

// Función para escribir un mensaje en el log y en la consola.
void escribirLog(AppConfig *config, char *cadena)
{

    // Esperar por el semáforo mutex antes de escribir en el archivo de log
    sem_wait(&config->mutex_log);

    FILE *logFile = fopen(config->log_file, "a");
    if (logFile == NULL)
    {
        perror("Error al abrir el archivo de log");
        // Liberar el semáforo mutex antes de regresar
        sem_post(&config->mutex_log);
        return;
    }
    fprintf(logFile, "%s:%s\n", cadena, config->path_files);
    fclose(logFile);

    // Liberar el semáforo mutex después de escribir
    sem_post(&config->mutex_log);
}

// Función para contar las líneas en un archivo
int contarLineas(const char *nombreArchivo)
{
    FILE *archivo = fopen(nombreArchivo, "r");
    if (!archivo)
    {
        perror("Error al abrir el archivo para contar líneas");
        return -1;
    }

    int numLineas = 0;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), archivo))
    {
        numLineas++;
    }

    fclose(archivo);
    return numLineas;
}

void ProcesarFichero(AppConfig *config, infoSucur *sucursal, int *index)
{
    DIR *dir;
    struct dirent *entry;
    FILE *archivo, *csvFile;
    char line[1024];

    dir = opendir(config->path_files);
    if (dir == NULL)
    {
        escribirLog(config, "Error al abrir el directorio:");
        return;
    }

    csvFile = fopen(config->inventory_file, "a");
    if (csvFile == NULL)
    {
        escribirLog(config, "Error al abrir el archivo CSV");
        closedir(dir);
        return;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        if (entry->d_type == DT_REG)
        {
            char filePath[512];
            snprintf(filePath, sizeof(filePath), "%s/%s", config->path_files, entry->d_name);

            char nuevofilePath[512];
            snprintf(nuevofilePath, sizeof(nuevofilePath), "%s/%s", config->ruta_procesados, entry->d_name);

            sem_wait(&config->file_semaphore);
            sleep(config->simulate_sleep);

            archivo = fopen(filePath, "r");
            if (archivo == NULL)
            {
                escribirLog(config, "Error al abrir el archivo de datos:");
                sem_post(&config->file_semaphore);
                continue;
            }

            while (fgets(line, sizeof(line), archivo) != NULL)
            {
                line[strcspn(line, "\n")] = 0;
                // Aquí necesitas parsear la línea y asignar los valores a la estructura infoSucur
                // Por ejemplo, con sscanf o strtok dependiendo del formato de tus datos
                // Asegúrate de que *index no exceda el tamaño del array sucursal

                // Suponiendo que parseamos con sscanf, y que line es algo así: "idOp;fechaIni;fechaFin;idUsu;tipoOpe;numOp;importe;estado;"
                sscanf(line, "%5[^;];%19[^;];%19[^;];%5[^;];%7[^;];%d;%f;%11[^;];",
                       sucursal[*index].idOp,
                       sucursal[*index].fechaIni,
                       sucursal[*index].fechaFin,
                       sucursal[*index].idUsu,
                       sucursal[*index].tipoOpe,
                       &sucursal[*index].numOp,
                       &sucursal[*index].importe,
                       sucursal[*index].estado);

                fprintf(csvFile, "%s\n", line);
                (*index)++;
            }

            fclose(archivo);
            escribirLog(config, "Archivo procesado:");
            if (rename (filePath, nuevofilePath) != 0){
                escribirLog(config, "No se pudo mover el archivo");
            }


            sem_post(&config->file_semaphore);
        }
    }

    fclose(csvFile);
    closedir(dir);
}

void initSemaphores(AppConfig *config)
{
    // Inicializar el semáforo mutex para el log
    sem_init(&config->mutex_log, 0, 1);
    // Inicializar el semáforo para controlar el número de archivos procesados simultáneamente
    sem_init(&config->file_semaphore, 0, config->num_procesos);
}
