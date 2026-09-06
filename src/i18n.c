#include "i18n.h"

#include <stdio.h>
#include <string.h>

typedef struct Translation {
	const char *spanish;
	const char *english;
	const char *italian;
} Translation;

static AppLanguage current_language = APP_LANGUAGE_SPANISH;

static const Translation translations[] = {
	{ "CRUZ: ELEGIR   CIRCULO: CERRAR", "CROSS: SELECT   CIRCLE: CLOSE", "CROCE: SCEGLI   CERCHIO: CHIUDI" },
	{ "TU ESPACIO", "YOUR SPACE", "IL TUO SPAZIO" },
	{ "EN VAGAROUTE", "ON VAGAROUTE", "SU VAGAROUTE" },
	{ "TU NOMBRE PERSONALIZA", "YOUR NAME PERSONALIZES", "IL TUO NOME PERSONALIZZA" },
	{ "LA EXPERIENCIA EN VAGAROUTE.", "YOUR VAGAROUTE EXPERIENCE.", "LA TUA ESPERIENZA VAGAROUTE." },
	{ "TODO SE GUARDA EN TU VITA.", "EVERYTHING IS SAVED ON YOUR VITA.", "TUTTO VIENE SALVATO SULLA TUA VITA." },
	{ "CONFIGURACION LOCAL.", "LOCAL SETTINGS.", "CONFIGURAZIONE LOCALE." },
	{ "INICIO RAPIDO.", "QUICK START.", "AVVIO RAPIDO." },
	{ "DATOS BAJO TU CONTROL.", "YOUR DATA, YOUR CONTROL.", "DATI SOTTO IL TUO CONTROLLO." },
	{ "ROUTER SEGURO", "SECURE ROUTER", "ROUTER SICURO" },
	{ "VAGAROUTE AI", "VAGAROUTE AI", "VAGAROUTE AI" },
	{ "CUAL ES TU NOMBRE?", "WHAT IS YOUR NAME?", "COME TI CHIAMI?" },
	{ "COMENZAREMOS PERSONALIZANDO", "WE WILL START BY PERSONALIZING", "INIZIEREMO PERSONALIZZANDO" },
	{ "TU ESPACIO LOCAL.", "YOUR LOCAL SPACE.", "IL TUO SPAZIO LOCALE." },
	{ "NOMBRE", "NAME", "NOME" },
	{ "IDIOMA", "LANGUAGE", "LINGUA" },
	{ "CONTINUAR", "CONTINUE", "CONTINUA" },
	{ "GUARDAR CAMBIOS", "SAVE CHANGES", "SALVA MODIFICHE" },
	{ "LA CONFIGURACION SE GUARDA SOLO", "SETTINGS ARE SAVED ONLY", "LA CONFIGURAZIONE VIENE SALVATA SOLO" },
	{ "EN UX0:DATA/VAGAROUTEAI.", "IN UX0:DATA/VAGAROUTEAI.", "IN UX0:DATA/VAGAROUTEAI." },
	{ "CRUZ: EDITAR   ARRIBA/ABAJO: CAMBIAR", "CROSS: EDIT   UP/DOWN: CHANGE", "CROCE: MODIFICA   SU/GIU: CAMBIA" },
	{ "START: SALIR", "START: EXIT", "START: ESCI" },
	{ "EDITA TU NOMBRE", "EDIT YOUR NAME", "MODIFICA IL TUO NOME" },
	{ "EDITA ENDPOINT", "EDIT ENDPOINT", "MODIFICA ENDPOINT" },
	{ "EDITA API KEY", "EDIT API KEY", "MODIFICA API KEY" },
	{ "ESCRIBE TU MENSAJE", "WRITE YOUR MESSAGE", "SCRIVI IL TUO MESSAGGIO" },
	{ "ESP", "ENG", "ITA" },
	{ "BOR", "DEL", "CANC" },
	{ "OK", "OK", "OK" },
	{ "Modelo", "Model", "Modello" },
	{ "No hay modelos", "No models", "Nessun modello" },
	{ "En linea", "Online", "Online" },
	{ "Verificando...", "Checking...", "Verifica..." },
	{ "Sin conexion", "Offline", "Offline" },
	{ "Nueva conversacion", "New conversation", "Nuova conversazione" },
	{ "Escribe una pregunta, pide ayuda o genera ideas.", "Ask a question, get help, or generate ideas.", "Fai una domanda, chiedi aiuto o genera idee." },
	{ "Explicame este", "Explain this", "Spiegami questo" },
	{ "Entiende y", "Understand and", "Capisci e" },
	{ "soluciona errores.", "solve errors.", "risolvi gli errori." },
	{ "Resume un texto", "Summarize a text", "Riassumi un testo" },
	{ "Obten un resumen", "Get a clear", "Ottieni un riassunto" },
	{ "claro y conciso.", "concise summary.", "chiaro e conciso." },
	{ "Genera ideas", "Generate ideas", "Genera idee" },
	{ "Brainstorm de", "Brainstorm", "Brainstorming di" },
	{ "ideas utiles.", "useful ideas.", "idee utili." },
	{ "Cargando.", "Loading.", "Caricamento." },
	{ "Cargando..", "Loading..", "Caricamento.." },
	{ "Cargando...", "Loading...", "Caricamento..." },
	{ "Pregunta sugerida", "Suggested question", "Domanda suggerita" },
	{ "Pulsa para consultar", "Tap to ask", "Tocca per chiedere" },
	{ "Consultando al", "Asking the", "Richiesta al" },
	{ "modelo...", "model...", "modello..." },
	{ "Sin sugerencia", "No suggestion", "Nessun suggerimento" },
	{ "No se pudo", "Could not", "Impossibile" },
	{ "Selecciona otro", "Select another", "Seleziona un altro" },
	{ "intentar luego.", "try again later.", "riprova piu tardi." },
	{ "Tu", "You", "Tu" },
	{ "Historial", "History", "Cronologia" },
	{ "Conversaciones guardadas en esta sesion.", "Conversations saved in this session.", "Conversazioni salvate in questa sessione." },
	{ "Aun no hay conversaciones.", "There are no conversations yet.", "Non ci sono ancora conversazioni." },
	{ "Conversacion activa", "Active conversation", "Conversazione attiva" },
	{ "Conversacion guardada", "Saved conversation", "Conversazione salvata" },
	{ "Libreria", "Library", "Libreria" },
	{ "Titulos instalados en la consola.", "Titles installed on the console.", "Titoli installati sulla console." },
	{ "No se encontraron titulos instalados.", "No installed titles were found.", "Nessun titolo installato trovato." },
	{ "Seleccionado", "Selected", "Selezionato" },
	{ "Pagina", "Page", "Pagina" },
	{ "D-PAD mover   CRUZ seleccionar   CIRCULO cerrar", "D-PAD move   CROSS select   CIRCLE close", "D-PAD muovi   CROCE seleziona   CERCHIO chiudi" },
	{ "CANCELA LA RESPUESTA ANTES DE ABRIR OTRO CHAT.", "CANCEL THE RESPONSE BEFORE OPENING ANOTHER CHAT.", "ANNULLA LA RISPOSTA PRIMA DI APRIRE UN'ALTRA CHAT." },
	{ "CARGANDO IDEAS DEL TITULO...", "LOADING TITLE IDEAS...", "CARICAMENTO IDEE DEL TITOLO..." },
	{ "NO SE PUDIERON CARGAR LAS IDEAS.", "COULD NOT LOAD THE IDEAS.", "IMPOSSIBILE CARICARE LE IDEE." },
	{ "CANCELA LA RESPUESTA ANTES DE CREAR OTRO CHAT.", "CANCEL THE RESPONSE BEFORE CREATING ANOTHER CHAT.", "ANNULLA LA RISPOSTA PRIMA DI CREARE UN'ALTRA CHAT." },
	{ "Escribe tu mensaje...", "Write your message...", "Scrivi il tuo messaggio..." },
	{ "Texto", "Text", "Testo" },
	{ "Imagen", "Image", "Immagine" },
	{ "Ajustes", "Settings", "Impostazioni" },
	{ "Configura tu conexion y tu perfil local.", "Configure your connection and local profile.", "Configura la connessione e il profilo locale." },
	{ "NOMBRE DE USUARIO", "USERNAME", "NOME UTENTE" },
	{ "Sin nombre configurado", "No name configured", "Nessun nome configurato" },
	{ "ENDPOINT URL", "ENDPOINT URL", "URL ENDPOINT" },
	{ "https://ejemplo.com/v1", "https://example.com/v1", "https://esempio.com/v1" },
	{ "API KEY", "API KEY", "API KEY" },
	{ "Sin API Key configurada", "No API key configured", "Nessuna API key configurata" },
	{ "Verificar conexion", "Check connection", "Verifica connessione" },
	{ "Volver al chat", "Back to chat", "Torna alla chat" },
	{ "CONEXION DISPONIBLE", "CONNECTION AVAILABLE", "CONNESSIONE DISPONIBILE" },
	{ "VERIFICANDO CONEXION...", "CHECKING CONNECTION...", "VERIFICA CONNESSIONE..." },
	{ "SIN CONEXION", "OFFLINE", "OFFLINE" },
	{ "CONFIG LOCAL", "LOCAL CONFIG", "CONFIG LOCALE" },
	{ "SELECT  Menu", "SELECT  Menu", "SELECT  Menu" },
	{ "D-PAD  Mover", "D-PAD  Move", "D-PAD  Muovi" },
	{ "TRIANGULO  Opciones", "TRIANGLE  Options", "TRIANGOLO  Opzioni" },
	{ "CRUZ  Aceptar", "CROSS  Accept", "CROCE  Accetta" },
	{ "CRUZ  Enviar", "CROSS  Send", "CROCE  Invia" },
	{ "No hay modelos disponibles", "No models available", "Nessun modello disponibile" },
	{ "ESCRIBE TU NOMBRE.", "ENTER YOUR NAME.", "INSERISCI IL TUO NOME." },
	{ "NO SE PUDO GUARDAR.", "COULD NOT SAVE.", "IMPOSSIBILE SALVARE." },
	{ "ESCRIBE UN MENSAJE.", "WRITE A MESSAGE.", "SCRIVI UN MESSAGGIO." },
	{ "SELECCIONA UN MODELO.", "SELECT A MODEL.", "SELEZIONA UN MODELLO." },
	{ "LA GENERACION DE IMAGENES SIGUE EN BETA.", "IMAGE GENERATION IS STILL IN BETA.", "LA GENERAZIONE DI IMMAGINI E ANCORA IN BETA." },
	{ "GENERANDO RESPUESTA...", "GENERATING RESPONSE...", "GENERAZIONE RISPOSTA..." },
	{ "CONFIGURA ENDPOINT Y API KEY.", "CONFIGURE ENDPOINT AND API KEY.", "CONFIGURA ENDPOINT E API KEY." },
	{ "EL ENDPOINT DEBE USAR HTTPS.", "THE ENDPOINT MUST USE HTTPS.", "L'ENDPOINT DEVE USARE HTTPS." },
	{ "RED NO DISPONIBLE.", "NETWORK UNAVAILABLE.", "RETE NON DISPONIBILE." },
	{ "RED NO CONECTADA.", "NETWORK NOT CONNECTED.", "RETE NON CONNESSA." },
	{ "ENDPOINT INVALIDO.", "INVALID ENDPOINT.", "ENDPOINT NON VALIDO." },
	{ "RESPUESTA INVALIDA.", "INVALID RESPONSE.", "RISPOSTA NON VALIDA." },
	{ "CONEXION VERIFICADA.", "CONNECTION VERIFIED.", "CONNESSIONE VERIFICATA." },
	{ "NO SE PUDO CARGAR EL HISTORIAL.", "COULD NOT LOAD HISTORY.", "IMPOSSIBILE CARICARE LA CRONOLOGIA." },
	{ "NOMBRE BORRADO.", "NAME DELETED.", "NOME ELIMINATO." },
	{ "CANCELANDO RESPUESTA...", "CANCELLING RESPONSE...", "ANNULLAMENTO RISPOSTA..." },
	{ "Recibiendo respuesta...", "Receiving response...", "Ricezione risposta..." },
	{ "La API rechazo las sugerencias.", "The API rejected the suggestions.", "L'API ha rifiutato i suggerimenti." },
	{ "La respuesta de sugerencias no es valida.", "The suggestions response is invalid.", "La risposta dei suggerimenti non e valida." },
	{ "Solicitud fallida.", "Request failed.", "Richiesta non riuscita." },
	{ "No se pudo iniciar la solicitud.", "Could not start the request.", "Impossibile avviare la richiesta." },
	{ "El mensaje no cabe en la solicitud.", "The message does not fit in the request.", "Il messaggio non entra nella richiesta." },
	{ "Memoria insuficiente para la solicitud.", "Not enough memory for the request.", "Memoria insufficiente per la richiesta." },
	{ "Solicitud cancelada.", "Request cancelled.", "Richiesta annullata." },
	{ "La respuesta de la API no es valida.", "The API response is invalid.", "La risposta dell'API non e valida." },
	{ "La API respondio sin contenido.", "The API returned no content.", "L'API non ha restituito contenuto." },
	{ "No se pudo guardar el historial.", "Could not save history.", "Impossibile salvare la cronologia." },
	{ "Error al guardar el historial local.", "Error saving local history.", "Errore nel salvataggio della cronologia locale." },
	{ "Historial local ignorado.", "Local history ignored.", "Cronologia locale ignorata." },
	{ "El historial guardado no es valido.", "Saved history is invalid.", "La cronologia salvata non e valida." },
	{ "Conectando...", "Connecting...", "Connessione..." },
	{ "No se pudo crear el hilo de red.", "Could not create the network thread.", "Impossibile creare il thread di rete." },
	{ "No se pudo arrancar el hilo de red.", "Could not start the network thread.", "Impossibile avviare il thread di rete." },
	{ "No se pudo crear el hilo de sugerencias.", "Could not create the suggestions thread.", "Impossibile creare il thread dei suggerimenti." },
	{ "No se pudo iniciar el hilo de sugerencias.", "Could not start the suggestions thread.", "Impossibile avviare il thread dei suggerimenti." },
	{ "Esperando respuesta...", "Waiting for response...", "In attesa della risposta..." },
	{ "Cancelando...", "Cancelling...", "Annullamento..." },
	{ "Fallo la conexion TLS.", "TLS connection failed.", "Connessione TLS non riuscita." },
	{ "No se pudo verificar el certificado TLS.", "Could not verify the TLS certificate.", "Impossibile verificare il certificato TLS." },
	{ "No se pudo resolver el servidor.", "Could not resolve the server.", "Impossibile risolvere il server." },
	{ "No se pudo conectar al servidor.", "Could not connect to the server.", "Impossibile connettersi al server." },
	{ "La solicitud agoto el tiempo.", "The request timed out.", "La richiesta e scaduta." },
	{ "Se interrumpio la conexion.", "The connection was interrupted.", "La connessione e stata interrotta." },
	{ "Memoria insuficiente para la solicitud.", "Not enough memory for the request.", "Memoria insufficiente per la richiesta." },
	{ "Error de red.", "Network error.", "Errore di rete." },
	{ "API key rechazada.", "API key rejected.", "API key rifiutata." },
	{ "Endpoint o modelo no encontrado.", "Endpoint or model not found.", "Endpoint o modello non trovato." },
	{ "Limite de solicitudes alcanzado.", "Request limit reached.", "Limite di richieste raggiunto." },
	{ "El servidor no esta disponible.", "The server is unavailable.", "Il server non e disponibile." },
	{ "La API rechazo la solicitud.", "The API rejected the request.", "L'API ha rifiutato la richiesta." },
};

void i18n_set_language(AppLanguage language) {
	if (language >= APP_LANGUAGE_COUNT) language = APP_LANGUAGE_SPANISH;
	current_language = language;
}

AppLanguage i18n_get_language(void) {
	return current_language;
}

const char *i18n_translate(const char *spanish) {
	if (spanish == NULL || current_language == APP_LANGUAGE_SPANISH) return spanish;
	for (size_t index = 0; index < sizeof(translations) / sizeof(translations[0]); ++index) {
		if (strcmp(spanish, translations[index].spanish) == 0) {
			return current_language == APP_LANGUAGE_ENGLISH ? translations[index].english : translations[index].italian;
		}
	}
	return spanish;
}

const char *i18n_language_code(AppLanguage language) {
	static const char *codes[] = { "es", "en", "it" };
	return language < APP_LANGUAGE_COUNT ? codes[language] : codes[0];
}

const char *i18n_language_name(AppLanguage language) {
	static const char *names[] = { "Espanol", "English", "Italiano" };
	return language < APP_LANGUAGE_COUNT ? names[language] : names[0];
}

AppLanguage i18n_next_language(AppLanguage language) {
	return (AppLanguage)((language + 1) % APP_LANGUAGE_COUNT);
}

AppLanguage i18n_previous_language(AppLanguage language) {
	return (AppLanguage)((language + APP_LANGUAGE_COUNT - 1) % APP_LANGUAGE_COUNT);
}

const char *i18n_normal_system_prompt(AppLanguage language) {
	static const char *prompts[] = {
		"Eres el asistente de VagaRoute AI dentro de VagaChatVITA, un cliente de chat que se ejecuta en PlayStation Vita. Responde en espanol salvo que el usuario pida otro idioma. Se claro, util y conciso.",
		"You are the VagaRoute AI assistant inside VagaChatVITA, a chat client running on PlayStation Vita. Reply in English unless the user asks for another language. Be clear, useful, and concise.",
		"Sei l'assistente VagaRoute AI dentro VagaChatVITA, un client di chat che funziona su PlayStation Vita. Rispondi in italiano salvo richiesta di un'altra lingua. Sii chiaro, utile e conciso."
	};
	return prompts[language < APP_LANGUAGE_COUNT ? language : 0];
}

int i18n_game_system_prompt(AppLanguage language, const char *title, const char *title_id,
	char *output, size_t capacity) {
	static const char *formats[] = {
		"El usuario preguntara sobre el titulo \"%s\" para PlayStation Vita (TITLE_ID: %s). Puede ser un juego o aplicacion publicado oficialmente para PS Vita o un proyecto homebrew. Responde de forma mas concisa y distingue la informacion confirmada de las inferencias. No inventes datos.",
		"The user will ask about the title \"%s\" for PlayStation Vita (TITLE_ID: %s). It may be a game or application officially released for PS Vita or a homebrew project. Reply more concisely and distinguish confirmed information from inferences. Do not invent facts.",
		"L'utente fara domande sul titolo \"%s\" per PlayStation Vita (TITLE_ID: %s). Potrebbe essere un gioco o un'applicazione pubblicata ufficialmente per PS Vita oppure un progetto homebrew. Rispondi in modo piu conciso e distingui le informazioni confermate dalle inferenze. Non inventare dati."
	};
	return snprintf(output, capacity, formats[language < APP_LANGUAGE_COUNT ? language : 0], title, title_id);
}

const char *i18n_suggestions_system_prompt(AppLanguage language) {
	static const char *prompts[] = {
		"Devuelve exclusivamente un JSON valido con la forma {\"suggestions\":[\"...\",\"...\",\"...\",\"...\"]}. Incluye exactamente cuatro preguntas breves en espanol que un usuario podria hacer sobre el titulo indicado para PlayStation Vita. Puede ser un juego o aplicacion publicado oficialmente para PS Vita o un proyecto homebrew. Adapta las preguntas sin inventar informacion ni asumir su origen. No uses markdown ni texto fuera del JSON.",
		"Return only valid JSON in this form: {\"suggestions\":[\"...\",\"...\",\"...\",\"...\"]}. Include exactly four brief questions in English that a user could ask about the indicated PlayStation Vita title. It may be a game or application officially released for PS Vita or a homebrew project. Adapt the questions without inventing facts or assuming its origin. Do not use markdown or text outside the JSON.",
		"Restituisci esclusivamente JSON valido in questa forma: {\"suggestions\":[\"...\",\"...\",\"...\",\"...\"]}. Includi esattamente quattro domande brevi in italiano che un utente potrebbe fare sul titolo PlayStation Vita indicato. Potrebbe essere un gioco o un'applicazione pubblicata ufficialmente per PS Vita oppure un progetto homebrew. Adatta le domande senza inventare informazioni o presumere l'origine. Non usare markdown o testo fuori dal JSON."
	};
	return prompts[language < APP_LANGUAGE_COUNT ? language : 0];
}

int i18n_suggestions_user_prompt(AppLanguage language, const char *title, const char *title_id,
	char *output, size_t capacity) {
	static const char *formats[] = {
		"Titulo para PlayStation Vita: %s\nTITLE_ID: %s\nGenera cuatro temas concretos para iniciar una conversacion sobre este titulo.",
		"PlayStation Vita title: %s\nTITLE_ID: %s\nGenerate four concrete topics to start a conversation about this title.",
		"Titolo per PlayStation Vita: %s\nTITLE_ID: %s\nGenera quattro temi concreti per iniziare una conversazione su questo titolo."
	};
	return snprintf(output, capacity, formats[language < APP_LANGUAGE_COUNT ? language : 0], title, title_id);
}
