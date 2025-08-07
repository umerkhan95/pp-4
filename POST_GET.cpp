#include "POST_GET.h"
#include <ArduinoJson.h>

/**
 * @brief Construct a new POSTGET::POSTGET object 
 * 
 */
POSTGET::POSTGET() 
  : _timeout(10000) {
}

/**
 * @brief Set the JWT token (optional - can be obtained through login)
 * 
 * @param token The JWT token to set
 */
void POSTGET::setToken(const String& token) {
  _jwtToken = token;
}

/**
 * @brief Perform a POST request
 * 
 * @param url The URL to send the POST request to
 * @param payload The JSON payload to send
 * @param retryOnAuthFailure Whether to retry on authentication failure
 * @return String The response from the server
 */
String POSTGET::post(const String& url, const JsonDocument& payload, bool retryOnAuthFailure) {
  String jsonPayload;
  serializeJson(payload, jsonPayload);
  return _makeRequest(url, "POST", jsonPayload, retryOnAuthFailure);
}

/**
 * @brief Perform a GET request
 * 
 * @param url The URL to send the GET request to
 * @param retryOnAuthFailure Whether to retry on authentication failure
 * @return String The response from the server
 */
String POSTGET::get(const String& url, bool retryOnAuthFailure) {
  return _makeRequest(url, "GET", "", retryOnAuthFailure);
}

/**
 * @brief Login and get token
 * 
 * @param loginUrl The URL to send the login request to
 * @param email The user's email
 * @param password The user's password
 * @return true if login was successful, false otherwise
 */
bool POSTGET::login(const String& loginUrl, const String& email, const String& password) {
  _loginUrl = loginUrl;
  _email = email;
  _password = password;
  
  StaticJsonDocument<200> loginDoc;
  loginDoc["email"] = _email;
  loginDoc["password"] = _password;
  
  String response = post(_loginUrl, loginDoc, false);
  
  if (response.length() == 0) {
    return false;
  }
  
  StaticJsonDocument<1024> respDoc;
  DeserializationError error = deserializeJson(respDoc, response);
  
  if (!error && respDoc["status"] == "success") {
    _jwtToken = respDoc["token"].as<String>();
    Serial.println("Token: " + _jwtToken);
    // Get device ID from response
    if (respDoc["user"]["device_id"].isNull()) {
      _deviceID = "1"; // Default device ID
    } else {
      _deviceID = String(respDoc["user"]["device_id"].as<int>());
    }
  
    Serial.println("Login successful!");
    Serial.println("Device ID: " + _deviceID);
    return true;
  }
  
  return false;
}

/**
 * @brief Check if the token is valid
 * 
 * @return true if the token is valid, false otherwise
 */
bool POSTGET::isTokenValid() const {
  return _jwtToken.length() > 0;
}

/**
 * @brief Set timeout for requests (in milliseconds)
 * 
 * @param timeout The timeout value in milliseconds
 */
void POSTGET::setTimeout(unsigned long timeout) {
  _timeout = timeout;
}

/**
 * @brief Get the current JWT token
 * 
 * @return String The current JWT token
 */
String POSTGET::getToken() const {
  return _jwtToken;
}

/**
 * @brief Get device ID
 * 
 * @return String The device ID
 */
String POSTGET::getDeviceID() const {
  return _deviceID;
}

/**
 * @brief Internal method to handle HTTP requests
 * 
 * @param url The URL to send the request to
 * @param method The HTTP method (GET or POST)
 * @param payload The JSON payload for POST requests
 * @param retryOnAuthFailure Whether to retry on authentication failure
 * @return String The response from the server
 */
String POSTGET::_makeRequest(const String& url, const String& method, const String& payload, bool retryOnAuthFailure) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "";
  }
  
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  if (_jwtToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + _jwtToken);
  }
  
  http.setTimeout(_timeout);
  
  int httpCode = 0;
  if (method == "POST") {
    httpCode = http.POST(payload);
  } else {
    httpCode = http.GET();
  }
  
  String response = http.getString();
  http.end();
  
  if (httpCode == 401 && retryOnAuthFailure) {
    Serial.println("Token expired or invalid. Attempting to refresh...");
    if (login(_loginUrl, _email, _password)) {
      // Retry the request with new token
      return _makeRequest(url, method, payload, false);
    }
    return "";
  }
  
  if (httpCode != 200) {
    Serial.print("HTTP ");
    Serial.print(method);
    Serial.print(" request failed with code: ");
    Serial.println(httpCode);
    Serial.println("Error: " + _parseError(response));
    return "";
  }
  
  return response;
}

/**
 * @brief Parse error from response
 * 
 * @param response The response string to parse
 * @return String The parsed error message
 */
String POSTGET::_parseError(const String& response) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, response);
  
  if (!error && doc.containsKey("message")) {
    return doc["message"].as<String>();
  }
  
  return response;
}