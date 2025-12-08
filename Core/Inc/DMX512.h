/**
 ******************************************************************************
 * @file    dmx512.h
 * @brief   Module de gestion du protocole DMX512
 * @author  Projet AnimLED
 ******************************************************************************
 * @attention
 *
 * Ce module gère la réception et le décodage des trames DMX512 via UART.
 *
 * Principe de fonctionnement :
 * 1. Le BREAK (erreur UART) détecte le début de trame
 * 2. Les 513 octets sont stockés dans un buffer
 * 3. Les données sont extraites selon l'adresse DMX configurée
 *
 * Configuration UART requise :
 * - Vitesse : 250000 bps
 * - Format : 8 bits, No parity, 2 stop bits (8N2)
 * - Mode : Asynchronous
 * - Interruptions : Rx et Error activées
 ******************************************************************************
 */

#ifndef __DMX512_H
#define __DMX512_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Définitions DMX512 --------------------------------------------------------*/
#define DMX_FRAME_SIZE          513     // START CODE (1) + 512 canaux
#define DMX_START_CODE          0x00    // Code de début de trame standard
#define DMX_CHANNELS_USED       5       // Nombre de canaux utilisés (R,G,B,DIM,FLASH)

#define DMX_CHANNEL_MIN         1       // Adresse DMX minimale
#define DMX_CHANNEL_MAX         506     // Adresse DMX maximale (512 - 5 canaux + 1)

/* Indices des canaux relatifs à l'adresse de base ------------------------------*/
#define DMX_OFFSET_RED          0       // Canal Rouge = Adresse + 0
#define DMX_OFFSET_GREEN        1       // Canal Vert = Adresse + 1
#define DMX_OFFSET_BLUE         2       // Canal Bleu = Adresse + 2
#define DMX_OFFSET_DIMMER       3       // Canal Dimmer = Adresse + 3
#define DMX_OFFSET_FLASH        4       // Canal Flash = Adresse + 4

/* Structure de données DMX --------------------------------------------------*/
/**
 * @brief Structure contenant les valeurs DMX décodées
 */
typedef struct {
    uint8_t red;        // Niveau de rouge (0-255)
    uint8_t green;      // Niveau de vert (0-255)
    uint8_t blue;       // Niveau de bleu (0-255)
    uint8_t dimmer;     // Niveau de luminosité globale (0-255)
    uint8_t flash;      // Fréquence de clignotement (0-255)
} DMX_DataTypeDef;

/* Variables globales (extern) -----------------------------------------------*/
extern volatile uint8_t dmxFlag;        // Flag : 1 = nouvelle trame disponible
extern volatile uint16_t indexDmx;      // Index de réception dans le buffer
extern uint8_t dmxFrame[DMX_FRAME_SIZE];// Buffer contenant la trame DMX complète
extern uint8_t rxBuf[1];                // Buffer de réception UART (1 octet)

/* Prototypes des fonctions --------------------------------------------------*/

/**
 * @brief  Initialise le module DMX
 * @param  huart: pointeur vers le handle UART configuré pour le DMX
 * @param  startChannel: adresse DMX de départ (1 à 506)
 * @retval None
 *
 * @note   Cette fonction doit être appelée après HAL_UART_Init()
 * @note   Elle lance la réception en interruption
 */
void DMX_Init(UART_HandleTypeDef *huart, uint8_t startChannel);

/**
 * @brief  Définit l'adresse DMX de départ
 * @param  channel: adresse DMX (1 à 506)
 * @retval true si l'adresse est valide, false sinon
 *
 * @note   L'adresse doit être comprise entre 1 et 506 car on utilise 5 canaux
 * @note   Exemple : si channel = 10, on utilise les canaux 10,11,12,13,14
 */
bool DMX_SetChannel(uint8_t channel);

/**
 * @brief  Récupère l'adresse DMX actuelle
 * @retval Adresse DMX de départ (1 à 506)
 */
uint8_t DMX_GetChannel(void);

/**
 * @brief  Vérifie si une nouvelle trame DMX est disponible
 * @retval true si une trame complète a été reçue, false sinon
 *
 * @note   Cette fonction doit être appelée régulièrement dans la boucle principale
 * @note   Le flag est automatiquement remis à zéro après lecture
 */
bool DMX_IsNewFrameAvailable(void);

/**
 * @brief  Décode la trame DMX et extrait les valeurs des canaux
 * @param  data: pointeur vers la structure qui recevra les données décodées
 * @retval true si le décodage est réussi, false si la trame est invalide
 *
 * @note   Vérifie le START CODE (doit être 0x00)
 * @note   Extrait les 5 canaux selon l'adresse DMX configurée
 * @note   N'applique PAS le dimmer aux couleurs (à faire dans le code appelant)
 */
bool DMX_DecodeFrame(DMX_DataTypeDef *data);

/**
 * @brief  Applique le coefficient dimmer aux valeurs RGB
 * @param  data: pointeur vers la structure contenant les données
 * @param  output: pointeur vers la structure qui recevra les valeurs finales
 * @retval None
 *
 * @note   Calcule : output.red = (data.red * data.dimmer) / 255
 * @note   Idem pour green et blue
 * @note   Le dimmer agit proportionnellement sur les 3 couleurs
 */
void DMX_ApplyDimmer(DMX_DataTypeDef *data, DMX_DataTypeDef *output);

/**
 * @brief  Callback à appeler depuis HAL_UART_ErrorCallback()
 * @param  huart: pointeur vers le handle UART
 * @retval None
 *
 * @note   Cette fonction détecte le BREAK DMX (erreur de trame UART)
 * @note   Elle réinitialise l'index de réception et relance la réception
 *
 * @example
 * void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
 *     DMX_ErrorCallback(huart);
 * }
 */
void DMX_ErrorCallback(UART_HandleTypeDef *huart);

/**
 * @brief  Callback à appeler depuis HAL_UART_RxCpltCallback()
 * @param  huart: pointeur vers le handle UART
 * @retval None
 *
 * @note   Cette fonction stocke chaque octet reçu dans le buffer
 * @note   Elle positionne le flag dmxFlag quand 513 octets sont reçus
 * @note   Elle relance automatiquement la réception pour l'octet suivant
 *
 * @example
 * void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *     DMX_RxCallback(huart);
 * }
 */
void DMX_RxCallback(UART_HandleTypeDef *huart);

/**
 * @brief  Récupère les statistiques de réception DMX
 * @param  totalFrames: pointeur pour recevoir le nombre total de trames reçues
 * @param  errorFrames: pointeur pour recevoir le nombre de trames erronées
 * @retval None
 *
 * @note   Utile pour le debug et la surveillance de la qualité du signal
 */
void DMX_GetStats(uint32_t *totalFrames, uint32_t *errorFrames);

/**
 * @brief  Réinitialise les statistiques de réception
 * @retval None
 */
void DMX_ResetStats(void);

#endif /* __DMX512_H */

/**
 ******************************************************************************
 * NOTES D'UTILISATION
 ******************************************************************************
 *
 * 1. Configuration UART dans STM32CubeMX :
 *    - Baud Rate: 250000
 *    - Word Length: 8 Bits
 *    - Parity: None
 *    - Stop Bits: 2
 *    - Activer "USART global interrupt"
 *
 * 2. Dans main.c, initialisation :
 *    DMX_Init(&huart1, 1);  // Démarre sur le canal 1
 *
 * 3. Dans stm32l4xx_it.c, callbacks UART :
 *    void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
 *        DMX_ErrorCallback(huart);
 *    }
 *
 *    void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
 *        DMX_RxCallback(huart);
 *    }
 *
 * 4. Dans la boucle principale :
 *    while(1) {
 *        if (DMX_IsNewFrameAvailable()) {
 *            DMX_DataTypeDef dmxData, finalData;
 *            if (DMX_DecodeFrame(&dmxData)) {
 *                DMX_ApplyDimmer(&dmxData, &finalData);
 *                // Appliquer finalData.red, green, blue aux PWM
 *            }
 *        }
 *    }
 *
 ******************************************************************************
 */
