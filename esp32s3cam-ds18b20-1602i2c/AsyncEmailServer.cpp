#include "AsyncEmailServer.h"
#include <ESP_Mail_Client.h>
#include "esp_log.h"

// Cấu hình SMTP
#define SMTP_HOST "smtp.gmail.com"
#define SMTP_PORT 587  // Cổng SSL

// Thông tin tài khoản Gmail
#define EMAIL_SENDER "hawyscloud@gmail.com"
#define EMAIL_PASSWORD "urtt anmc dpiv wdnv"
#define EMAIL_RECIPIENT "hiepthai12@gmail.com"

SMTPSession smtp;

void sendEmail(const char *subject, const char *body) {
  // Cấu hình thông tin kết nối SMTP
  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = EMAIL_SENDER;
  session.login.password = EMAIL_PASSWORD;
  session.secure.startTLS = false;  // Sử dụng SSL trực tiếp

  // Gửi email
  if (!smtp.connect(&session)) {
    Serial.println("Kết nối SMTP thất bại!");
    return;
  }

  // Thiết lập nội dung email
  SMTP_Message message;
  message.sender.name     = "ESP32 Sender";
  message.sender.email    = EMAIL_SENDER;
  message.subject         = subject;
  message.text.content    = body;
  message.addRecipient("Bạn", EMAIL_RECIPIENT);

  if (!MailClient.sendMail(&smtp, &message)) {
    Serial.print("Gửi email thất bại: ");
    Serial.println(smtp.errorReason().c_str());
  } else {
    Serial.println("Email đã được gửi thành công!");
  }

  // Kết thúc phiên SMTP
  smtp.closeSession();
}
