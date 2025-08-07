#ifndef _POST_GET_H_
#define _POST_GET_H_

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

class POSTGET {
  public:
    // Constructor
    POSTGET();

    // Set the JWT token (optional - can be obtained through login)
    void setToken(const String& token);
    
    // Perform a POST request
    String post(const String& url, const JsonDocument& payload, bool retryOnAuthFailure = true);
    
    // Perform a GET request
    String get(const String& url, bool retryOnAuthFailure = true);
    
    // Login and get token
    bool login(const String& loginUrl, const String& email, const String& password);
    
    // Check if token is valid
    bool isTokenValid() const;
    
    // Set timeout for requests (in milliseconds)
    void setTimeout(unsigned long timeout);
    
    // Get the current JWT token
    String getToken() const;

    // Get deviceID
    String getDeviceID() const;
    
  private:
    String _loginUrl;
    String _email;
    String _password;
    String _jwtToken;
    String _deviceID;
    unsigned long _timeout;
    
    // Internal method to handle HTTP requests
    String _makeRequest(const String& url, const String& method, const String& payload = "", bool retryOnAuthFailure = true);
    
    // Parse error from response
    String _parseError(const String& response);
};

#endif //_POST_GET_H_