/**
 * @file    sonoff_html.h
 * @brief   Sonoff静态网页.
 *
 * @author  yifei wang (yifei.wang@itead.cc)
 * @date    2026-08-26
 *
 * @copyright Copyright (c) 2026  深圳松诺技术有限公司
 *
 */
#ifndef __SONOFF_HTML_H__
#define __SONOFF_HTML_H__

/** @brief HTTP首页内容. */
static const char sonoff_html_index[] =
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "<head><meta charset=\"utf-8\"><title>Sonoff</title></head>\r\n"
    "<body>\r\n"
    "<h1>Hello World</h1>\r\n"
    "<span>GPIO output: </span>\r\n"
    "<span id=\"toggle-status\">unknown</span>\r\n"
    "<br><br>\r\n"
    "<span>Current time: </span>\r\n"
    "<span id=\"current-time\">2026.08.26-09:00:00</span>\r\n"
    "<br><br>\r\n"
    "<form action=\"/cgi/toggle\" method=\"get\">\r\n"
    "<button type=\"submit\">toggle</button>\r\n"
    "</form>\r\n"
    "<br><br>\r\n"
    "<h2>Firmware OTA</h2>\r\n"
    "<input id=\"ota-file\" type=\"file\" accept=\".bin,application/octet-stream\">\r\n"
    "<button id=\"ota-upload\" type=\"button\">Upgrade</button>\r\n"
    "<span id=\"ota-progress\">0%</span>\r\n"
    "<span id=\"ota-status\"></span>\r\n"
    "<script>\r\n"
    "function updateLabel(path, id) {\r\n"
    "  fetch(path).then(function(response) { return response.text(); })\r\n"
    "    .then(function(value) { document.getElementById(id).textContent = value; });\r\n"
    "}\r\n"
    "function updatePage() {\r\n"
    "  updateLabel('/cgi/toggle_status', 'toggle-status');\r\n"
    "  updateLabel('/cgi/time', 'current-time');\r\n"
    "}\r\n"
    "function setOtaProgress(value) {\r\n"
    "  document.getElementById('ota-progress').textContent = value + '%';\r\n"
    "}\r\n"
    "function uploadOta() {\r\n"
    "  var fileInput = document.getElementById('ota-file');\r\n"
    "  var statusLabel = document.getElementById('ota-status');\r\n"
    "  var uploadButton = document.getElementById('ota-upload');\r\n"
    "  if (fileInput.files.length === 0) {\r\n"
    "    statusLabel.textContent = ' Select a firmware file.';\r\n"
    "    return;\r\n"
    "  }\r\n"
    "  var request = new XMLHttpRequest();\r\n"
    "  var file = fileInput.files[0];\r\n"
    "  uploadButton.disabled = true;\r\n"
    "  statusLabel.textContent = ' Uploading...';\r\n"
    "  setOtaProgress(0);\r\n"
    "  clearInterval(pageTimer);\r\n"
    "  request.open('POST', '/cgi/"
    "ota_upload', true);\r\n"
    "  request.setRequestHeader('Content-Type', 'application/octet-stream');\r\n"
    "  request.upload.onprogress = function(event) {\r\n"
    "    if (event.lengthComputable) {\r\n"
    "      setOtaProgress(Math.floor(event.loaded * 100 / event.total));\r\n"
    "    }\r\n"
    "  };\r\n"
    "  request.onload = function() {\r\n"
    "    uploadButton.disabled = false;\r\n"
    "    pageTimer = setInterval(updatePage, 1000);\r\n"
    "    if (request.status === 200 && request.responseText.indexOf('OTA upload success') === 0) {\r\n"
    "      setOtaProgress(100);\r\n"
    "      statusLabel.textContent = ' ' + request.responseText;\r\n"
    "    } else {\r\n"
    "      statusLabel.textContent = ' Upload failed.';\r\n"
    "    }\r\n"
    "  };\r\n"
    "  request.onerror = function() {\r\n"
    "    uploadButton.disabled = false;\r\n"
    "    pageTimer = setInterval(updatePage, 1000);\r\n"
    "    statusLabel.textContent = ' Upload connection failed.';\r\n"
    "  };\r\n"
    "  request.send(file);\r\n"
    "}\r\n"
    "document.getElementById('ota-upload').onclick = uploadOta;\r\n"
    "updatePage();\r\n"
    "var pageTimer = setInterval(updatePage, 1000);\r\n"
    "</script>\r\n"
    "</body>\r\n"
    "</html>\r\n";

#endif /* __SONOFF_HTML_H__ */
