/**
 ******************************************************************************
 * @file    dmx512.c
 * @brief   Implémentation du module de gestion DMX512
 * @author  Projet AnimLED
 ******************************************************************************
 */

#include "dmx512.h"
#include <string.h>

/* Variables privées ---------------------------------------------------------*/
static UART_HandleTypeDef *huart_dmx = NULL;    // Handle UART pour le DMX
static uint8_t dmxChannel = 1;                   // Adresse DMX de départ (défaut = 1)

/* Statistiques (optionnel pour debug) */
static uint32_t totalFramesReceived = 0;         // Compteur de trames reçues
static uint32_t errorFramesReceived = 0;         // Compteur de trames erronées

/* Variables globales (accessibles depuis l'extérieur) -----------------------*/
volatile uint8_t dmxFlag = 0;                    // Flag : nouvelle trame disponible
volatile uint16_t indexDmx = 0;                  // Index courant dans le buffer
uint8_t dmxFrame[DMX_FRAME_SIZE] = {0};          // Buffer de la trame DMX
uint8_t rxBuf[1] = {0};                          // Buffer réception UART (1 octet)

/* Implémentation des fonctions ----------------------------------------------*/

/**
 * @brief  Initialise le module DMX
 */
void DMX_Init(UART_HandleTypeDef *huart, uint8_t startChannel)
{
    // Sauvegarde du handle UART
    huart_dmx = huart;

    // Configuration de l'adresse DMX
    DMX_SetChannel(startChannel);

    // Initialisation des variables
    indexDmx = 0;
    dmxFlag = 0;
    memset(dmxFrame, 0, DMX_FRAME_SIZE);

    // Réinitialisation des statistiques
    DMX_ResetStats();

    // Lancement de la réception en interruption (1 octet à la fois)
    HAL_UART_Receive_IT(huart_dmx, rxBuf, 1);
}

/**
 * @brief  Définit l'adresse DMX de départ
 */
bool DMX_SetChannel(uint8_t channel)
{
    // Vérification de la validité de l'adresse
    // On utilise 5 canaux, donc l'adresse max est 512 - 5 + 1 = 508
    // Mais dans le sujet c'est indiqué 506, donc on respecte cette contrainte
    if (channel >= DMX_CHANNEL_MIN && channel <= DMX_CHANNEL_MAX) {
        dmxChannel = channel;
        return true;
    }
    return false;
}

/**
 * @brief  Récupère l'adresse DMX actuelle
 */
uint8_t DMX_GetChannel(void)
{
    return dmxChannel;
}

/**
 * @brief  Vérifie si une nouvelle trame DMX est disponible
 */
bool DMX_IsNewFrameAvailable(void)
{
    if (dmxFlag == 1) {
        dmxFlag = 0;  // Acquittement automatique du flag
        return true;
    }
    return false;
}

/**
 * @brief  Décode la trame DMX et extrait les valeurs des canaux
 */
bool DMX_DecodeFrame(DMX_DataTypeDef *data)
{
    // Vérification du pointeur
    if (data == NULL) {
        return false;
    }

    // Vérification du START CODE (doit être 0x00 pour une trame standard)
    if (dmxFrame[0] != DMX_START_CODE) {
        errorFramesReceived++;
        return false;
    }

    // Extraction des 5 canaux selon l'adresse DMX configurée
    // Note : dmxFrame[0] = START CODE
    //        dmxFrame[1] = Canal 1
    //        dmxFrame[2] = Canal 2, etc.
    // Donc pour accéder au canal N, on utilise dmxFrame[N]

    data->red    = dmxFrame[dmxChannel + DMX_OFFSET_RED];
    data->green  = dmxFrame[dmxChannel + DMX_OFFSET_GREEN];
    data->blue   = dmxFrame[dmxChannel + DMX_OFFSET_BLUE];
    data->dimmer = dmxFrame[dmxChannel + DMX_OFFSET_DIMMER];
    data->flash  = dmxFrame[dmxChannel + DMX_OFFSET_FLASH];

    // Incrémentation du compteur de trames valides
    totalFramesReceived++;

    return true;
}

/**
 * @brief  Applique le coefficient dimmer aux valeurs RGB
 *
 * Explications :
 * Le dimmer est un coefficient multiplicateur global qui agit sur les 3 couleurs.
 *
 * Exemple :
 * - Rouge = 200, Vert = 150, Bleu = 100, Dimmer = 128 (50%)
 * - Résultat : Rouge = 200*128/255 = 100
 *              Vert  = 150*128/255 = 75
 *              Bleu  = 100*128/255 = 50
 *
 * Cela permet de baisser la luminosité globale sans changer les proportions de couleurs.
 */
void DMX_ApplyDimmer(DMX_DataTypeDef *data, DMX_DataTypeDef *output)
{
    if (data == NULL || output == NULL) {
        return;
    }

    // Calcul avec le dimmer (multiplication puis division par 255)
    // Note : on fait la multiplication en 32 bits pour éviter les débordements
    output->red   = ((uint32_t)data->red   * (uint32_t)data->dimmer) / 255;
    output->green = ((uint32_t)data->green * (uint32_t)data->dimmer) / 255;
    output->blue  = ((uint32_t)data->blue  * (uint32_t)data->dimmer) / 255;

    // Les autres valeurs sont simplement copiées
    output->dimmer = data->dimmer;
    output->flash  = data->flash;
}

/**
 * @brief  Callback d'erreur UART - Détection du BREAK DMX
 *
 * Explications détaillées :
 *
 * Dans le protocole DMX, le BREAK est un niveau BAS de plus de 88µs.
 * Pour l'UART, c'est une anomalie car :
 * - Un octet normal dure 44µs (11 bits × 4µs)
 * - L'UART détecte un START bit (front descendant)
 * - Mais la ligne reste BASSE trop longtemps
 * - Résultat : ERREUR DE TRAME (framing error)
 *
 * Cette erreur est notre signal pour détecter le début d'une trame DMX !
 *
 * Que fait cette fonction ?
 * 1. Réinitialise l'index de réception à 0 (début de trame)
 * 2. Remet le flag à 0 (trame non complète)
 * 3. Relance la réception UART pour l'octet suivant
 */
void DMX_ErrorCallback(UART_HandleTypeDef *huart)
{
    // Vérifier que c'est bien notre UART DMX
    if (huart == huart_dmx) {
        // Réinitialisation pour recevoir une nouvelle trame
        indexDmx = 0;
        dmxFlag = 0;

        // Relancer la réception en interruption
        // Note : HAL_UART_Receive_IT est non-bloquant
        // Il configure l'UART pour générer une interruption à la réception du prochain octet
        HAL_UART_Receive_IT(huart_dmx, rxBuf, 1);
    }
}

/**
 * @brief  Callback de réception UART - Réception d'un octet DMX
 *
 * Explications détaillées :
 *
 * Cette fonction est appelée chaque fois qu'un octet est reçu sur l'UART.
 *
 * Déroulement :
 * 1. BREAK détecté → indexDmx = 0 (via DMX_ErrorCallback)
 * 2. START CODE reçu → stocké dans dmxFrame[0], indexDmx = 1
 * 3. Canal 1 reçu → stocké dans dmxFrame[1], indexDmx = 2
 * 4. Canal 2 reçu → stocké dans dmxFrame[2], indexDmx = 3
 * ... et ainsi de suite jusqu'au canal 512
 * 5. Quand indexDmx atteint 513 → trame complète, dmxFlag = 1
 *
 * Protection :
 * - On vérifie que indexDmx < 513 pour éviter un débordement de buffer
 * - La réception est relancée automatiquement pour l'octet suivant
 */
void DMX_RxCallback(UART_HandleTypeDef *huart)
{
    // Vérifier que c'est bien notre UART DMX
    if (huart == huart_dmx) {

        // Stocker l'octet reçu dans le buffer (si pas de débordement)
        if (indexDmx < DMX_FRAME_SIZE) {
            dmxFrame[indexDmx] = rxBuf[0];
            indexDmx++;

            // Si on a reçu tous les octets de la trame (513 au total)
            if (indexDmx >= DMX_FRAME_SIZE) {
                dmxFlag = 1;  // Signaler qu'une trame complète est disponible
                // Note : on ne remet PAS indexDmx à 0 ici
                // C'est le prochain BREAK qui le fera via DMX_ErrorCallback
            }
        }

        // Relancer la réception pour le prochain octet
        // Important : sans cet appel, on ne recevrait qu'un seul octet !
        HAL_UART_Receive_IT(huart_dmx, rxBuf, 1);
    }
}

/**
 * @brief  Récupère les statistiques de réception DMX
 */
void DMX_GetStats(uint32_t *totalFrames, uint32_t *errorFrames)
{
    if (totalFrames != NULL) {
        *totalFrames = totalFramesReceived;
    }
    if (errorFrames != NULL) {
        *errorFrames = errorFramesReceived;
    }
}

/**
 * @brief  Réinitialise les statistiques de réception
 */
void DMX_ResetStats(void)
{
    totalFramesReceived = 0;
    errorFramesReceived = 0;
}

/**
 ******************************************************************************
 * EXPLICATIONS COMPLÉMENTAIRES
 ******************************************************************************
 *
 * 1. POURQUOI L'ERREUR UART DÉTECTE LE BREAK ?
 *
 *    Trame UART normale (1 octet) :
 *
 *    ┌─────┬──┬──┬──┬──┬──┬──┬──┬──┬───┬───┐
 *    │START│D0│D1│D2│D3│D4│D5│D6│D7│STP│STP│  = 44µs total
 *    └─────┴──┴──┴──┴──┴──┴──┴──┴──┴───┴───┘
 *      4µs  (8 bits données)       (2 bits stop)
 *
 *    BREAK DMX :
 *
 *    ┌─────────────────────────────────────┐
 *    │           NIVEAU BAS                │  > 88µs
 *    └─────────────────────────────────────┘
 *      ↑
 *      L'UART voit un START bit... mais la ligne reste BASSE !
 *      Il attend les bits de données... mais ils ne viennent jamais !
 *      → ERREUR DE TRAME (framing error)
 *
 * 2. DÉROULEMENT COMPLET D'UNE RÉCEPTION DMX :
 *
 *    Temps    Signal           Action
 *    ─────────────────────────────────────────────────────────
 *    t0       BREAK            → DMX_ErrorCallback()
 *                              → indexDmx = 0
 *
 *    t1       MAB (8µs)        → (transition, pas d'interruption)
 *
 *    t2       START CODE       → DMX_RxCallback()
 *                              → dmxFrame[0] = 0x00
 *                              → indexDmx = 1
 *
 *    t3       Canal 1          → DMX_RxCallback()
 *                              → dmxFrame[1] = valeur
 *                              → indexDmx = 2
 *
 *    ...      (511 autres)     → (même chose)
 *
 *    t514     Canal 512        → DMX_RxCallback()
 *                              → dmxFrame[512] = valeur
 *                              → indexDmx = 513
 *                              → dmxFlag = 1 ✓ TRAME COMPLÈTE !
 *
 *    t515     IDLE             → (pause avant la prochaine trame)
 *
 *    t516     BREAK            → (on recommence)
 *
 * 3. GESTION DE L'INDEX :
 *
 *    indexDmx pointe toujours vers la PROCHAINE case à remplir.
 *
 *    Exemple :
 *    - BREAK → indexDmx = 0
 *    - Réception octet → stocké dans dmxFrame[0], puis indexDmx = 1
 *    - Réception octet → stocké dans dmxFrame[1], puis indexDmx = 2
 *    - etc.
 *
 *    Quand indexDmx atteint 513, cela signifie que les cases 0 à 512 sont remplies.
 *
 * 4. ADRESSAGE DMX :
 *
 *    dmxFrame[0]   = START CODE (0x00)
 *    dmxFrame[1]   = Canal DMX n°1
 *    dmxFrame[2]   = Canal DMX n°2
 *    ...
 *    dmxFrame[512] = Canal DMX n°512
 *
 *    Si notre appareil est configuré sur le canal 10 (dmxChannel = 10) :
 *    - Rouge  = dmxFrame[10]  (canal 10)
 *    - Vert   = dmxFrame[11]  (canal 11)
 *    - Bleu   = dmxFrame[12]  (canal 12)
 *    - Dimmer = dmxFrame[13]  (canal 13)
 *    - Flash  = dmxFrame[14]  (canal 14)
 *
 ******************************************************************************
 */
