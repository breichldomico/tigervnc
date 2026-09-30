/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <algorithm>
#include <libgen.h>
#include <vector>

// FIXME: Workaround for FLTK including windows.h
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_utf8.h>

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"
#include "CConn.h"

static core::LogWriter vlog("ServerDialog");

const char* SERVER_HISTORY="tigervnc.history";

static bool same_server(const std::string& a, const std::string& b)
{
  std::string hostA, hostB;
  int portA, portB;

#ifndef WIN32
  if ((a.find("/") != std::string::npos) ||
      (b.find("/") != std::string::npos))
    return a == b;
#endif

  try {
    network::getHostAndPort(a.c_str(), &hostA, &portA);
    network::getHostAndPort(b.c_str(), &hostB, &portB);
  } catch (std::exception&) {
    return false;
  }

  if (hostA != hostB)
    return false;

  if (portA != portB)
    return false;

  return true;
}

ServerDialog::ServerDialog()
  : Fl_Window(520, 0, _("TigerVNC"))
{
  int x, y, x2;
  Fl_Button *button;
  Fl_Box *divider;

  x = OUTER_MARGIN;
  y = OUTER_MARGIN;

  Fl_Box *clientTitle = new Fl_Box(x, y, w() - OUTER_MARGIN*2, 20, _("Clients / Servers:"));
  clientTitle->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  clientTitle->labelfont(FL_HELVETICA_BOLD);
  y += 22;

  clientBrowser = new Fl_Hold_Browser(x, y, w() - OUTER_MARGIN*2, 170);
  static const int colWidths[] = { 260, 200, 0 };
  clientBrowser->column_widths(colWidths);
  clientBrowser->column_char('\t');
  clientBrowser->callback(this->handleClientSelect, this);
  clientBrowser->when(FL_WHEN_CHANGED | FL_WHEN_ENTER_KEY_ALWAYS);

  y += 170 + INNER_MARGIN;

  int labelW = 100;
  int inputW = w() - OUTER_MARGIN*2 - labelW;

  serverName = new Fl_Input(x + labelW, y, inputW, INPUT_HEIGHT, _("Server / IP:"));
  serverName->align(FL_ALIGN_LEFT);

  y += INPUT_HEIGHT + INNER_MARGIN;

  userName = new Fl_Input(x + labelW, y, inputW, INPUT_HEIGHT, _("Login User:"));
  userName->align(FL_ALIGN_LEFT);

  y += INPUT_HEIGHT + INNER_MARGIN;

  x2 = x;

  btnAddUpdate = new Fl_Button(x2, y, 130, BUTTON_HEIGHT, _("Save Client"));
  btnAddUpdate->callback(this->handleAddOrUpdateClient, this);
  x2 += 130 + INNER_MARGIN;

  btnDelete = new Fl_Button(x2, y, 120, BUTTON_HEIGHT, _("Delete Client"));
  btnDelete->callback(this->handleDeleteClient, this);
  x2 += 120 + INNER_MARGIN;

  btnClear = new Fl_Button(x2, y, 80, BUTTON_HEIGHT, _("New"));
  btnClear->callback(this->handleClearInputs, this);
  x2 += 80 + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  x2 = x;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Options..."));
  button->callback(this->handleOptions, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Load..."));
  button->callback(this->handleLoad, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Save as..."));
  button->callback(this->handleSaveAs, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  divider = new Fl_Box(0, y, w(), 2);
  divider->box(FL_THIN_DOWN_FRAME);

  y += divider->h() + INNER_MARGIN;

  // Symmetric margin around bottom button bar
  y += OUTER_MARGIN - INNER_MARGIN;

  button = new Fl_Button(x, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("About..."));
  button->callback(this->handleAbout, this);

  x2 = w() - OUTER_MARGIN - BUTTON_WIDTH*2 - INNER_MARGIN*1;

  button = new Fl_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Cancel"));
  button->callback(this->handleCancel, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  button = new Fl_Return_Button(x2, y, BUTTON_WIDTH, BUTTON_HEIGHT, _("Connect"));
  button->callback(this->handleConnect, this);
  x2 += BUTTON_WIDTH + INNER_MARGIN;

  y += BUTTON_HEIGHT + INNER_MARGIN;

  /* Needed for resize to work sanely */
  resizable(nullptr);
  h(y-INNER_MARGIN+OUTER_MARGIN);

  callback(this->handleCancel, this);
}

ServerDialog::~ServerDialog()
{
}

void ServerDialog::run(const char* servername, char *newservername)
{
  ServerDialog dialog;

  if (servername && servername[0] != '\0')
    dialog.serverName->value(servername);

  try {
    dialog.loadClients();
    dialog.refreshClientBrowser();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load clients: %s"), e.what());
  }

  try {
    dialog.loadServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load the server history: %s"), e.what());
  }

  // Pre-select matching client or first client
  if (servername && servername[0] != '\0') {
    for (size_t i = 0; i < dialog.clients.size(); ++i) {
      if (same_server(dialog.clients[i].ip, servername)) {
        dialog.clientBrowser->value((int)i + 2);
        dialog.userName->value(dialog.clients[i].username.c_str());
        break;
      }
    }
  } else if (!dialog.clients.empty()) {
    dialog.clientBrowser->value(2);
    dialog.serverName->value(dialog.clients[0].ip.c_str());
    dialog.userName->value(dialog.clients[0].username.c_str());
  }

  dialog.show();

  while (dialog.shown()) Fl::wait();

  if (dialog.serverName->value() == nullptr || dialog.serverName->value()[0] == '\0') {
    newservername[0] = '\0';
    return;
  }

  strncpy(newservername, dialog.serverName->value(), VNCSERVERNAMELEN);
  newservername[VNCSERVERNAMELEN - 1] = '\0';
}

void ServerDialog::handleOptions(Fl_Widget* /*widget*/, void* /*data*/)
{
  OptionsDialog::showDialog();
}

void ServerDialog::handleLoad(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();

  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      0, _("Select a TigerVNC configuration file"));
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  // Block until user picks something.
  while(file_chooser->shown())
    Fl::wait();
  
  // Did the user hit cancel?
  if (file_chooser->value() == nullptr) {
    delete(file_chooser);
    return;
  }
  
  const char* filename = file_chooser->value();
  dialog->updateUsedDir(filename);

  try {
    dialog->serverName->value(loadViewerParameters(filename));
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to load the specified configuration file:\n\n%s"),
             e.what());
  }

  delete(file_chooser);
}

void ServerDialog::handleSaveAs(Fl_Widget* /*widget*/, void* data)
{ 
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  const char* filename;
  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();
  
  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TigerVNC configuration (*.tigervnc)"),
                                                      2, _("Save the TigerVNC configuration to file"));
  
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  while(1) {
    
    // Block until user picks something.
    while(file_chooser->shown())
      Fl::wait();
    
    // Did the user hit cancel?
    if (file_chooser->value() == nullptr) {
      delete(file_chooser);
      return;
    }
    
    filename = file_chooser->value();
    dialog->updateUsedDir(filename);
    
    FILE* f = fopen(filename, "r");
    if (f) {

      // The file already exists.
      fclose(f);
      int overwrite_choice = fl_choice(_("%s already exists. Do you want to overwrite?"), 
                                       _("Overwrite"), _("No"), nullptr, filename);
      if (overwrite_choice == 1) {

        // If the user doesn't want to overwrite:
        file_chooser->show();
        continue;
      }
    }

    break;
  }
  
  try {
    saveViewerParameters(filename, servername);
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to save the specified configuration "
               "file:\n\n%s"), e.what());
  }
  
  delete(file_chooser);
}

void ServerDialog::handleAbout(Fl_Widget* /*widget*/, void* /*data*/)
{
  about_vncviewer();
}

void ServerDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->serverName->value("");
  dialog->hide();
}

void ServerDialog::handleConnect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  const char* username = dialog->userName->value();

  if (!servername || servername[0] == '\0') {
    fl_alert(_("Please enter or select a server IP / hostname."));
    return;
  }

  if (username && username[0] != '\0')
    CConn::setSavedUsername(username);
  else
    CConn::setSavedUsername("");

  // Auto-record / update client in list
  bool found = false;
  std::string sName = servername;
  std::string uName = username ? username : "";
  for (size_t i = 0; i < dialog->clients.size(); ++i) {
    if (same_server(dialog->clients[i].ip, sName)) {
      if (!uName.empty())
        dialog->clients[i].username = uName;
      found = true;
      break;
    }
  }
  if (!found) {
    ClientEntry ce;
    ce.ip = sName;
    ce.username = uName;
    dialog->clients.push_back(ce);
  }

  try {
    dialog->saveClients();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save clients: %s"), e.what());
  }

  dialog->hide();

  try {
    saveViewerParameters(nullptr, servername);
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the default configuration: %s"),
               e.what());
  }

  // avoid duplicates in the history
  dialog->serverHistory.remove(servername);
  dialog->serverHistory.insert(dialog->serverHistory.begin(), servername);

  try {
    dialog->saveServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the server history: %s"), e.what());
  }
}

void ServerDialog::handleClientSelect(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int selected = dialog->clientBrowser->value();
  if (selected <= 1)
    return;

  size_t idx = (size_t)(selected - 2);
  if (idx < dialog->clients.size()) {
    dialog->serverName->value(dialog->clients[idx].ip.c_str());
    dialog->userName->value(dialog->clients[idx].username.c_str());
  }

  if (Fl::event_clicks()) {
    Fl::event_clicks(0);
    handleConnect(nullptr, dialog);
  }
}

void ServerDialog::handleAddOrUpdateClient(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* ip = dialog->serverName->value();
  const char* user = dialog->userName->value();

  if (!ip || ip[0] == '\0') {
    fl_alert(_("Please enter a server IP or hostname."));
    return;
  }

  std::string strIP = ip;
  std::string strUser = user ? user : "";

  bool updated = false;
  for (size_t i = 0; i < dialog->clients.size(); ++i) {
    if (same_server(dialog->clients[i].ip, strIP)) {
      dialog->clients[i].username = strUser;
      updated = true;
      break;
    }
  }

  if (!updated) {
    ClientEntry ce;
    ce.ip = strIP;
    ce.username = strUser;
    dialog->clients.push_back(ce);
  }

  dialog->saveClients();
  dialog->refreshClientBrowser();

  // Re-select this item
  for (size_t i = 0; i < dialog->clients.size(); ++i) {
    if (same_server(dialog->clients[i].ip, strIP)) {
      dialog->clientBrowser->value((int)i + 2);
      break;
    }
  }
}

void ServerDialog::handleDeleteClient(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int selected = dialog->clientBrowser->value();
  if (selected <= 1) {
    fl_alert(_("Please select a client from the list to delete."));
    return;
  }

  size_t idx = (size_t)(selected - 2);
  if (idx < dialog->clients.size()) {
    dialog->clients.erase(dialog->clients.begin() + idx);
    dialog->saveClients();
    dialog->refreshClientBrowser();
    dialog->serverName->value("");
    dialog->userName->value("");
  }
}

void ServerDialog::handleClearInputs(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->clientBrowser->deselect();
  dialog->serverName->value("");
  dialog->userName->value("");
}

void ServerDialog::refreshClientBrowser()
{
  clientBrowser->clear();
  clientBrowser->add(_("@b@.Server / IP\t@b@.Login User"));
  for (const auto& client : clients) {
    std::string row = client.ip + "\t" + client.username;
    clientBrowser->add(row.c_str());
  }
}

#ifdef _WIN32
void ServerDialog::loadClients()
{
  clients.clear();
  HKEY hKey;
  LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                           L"Software\\TigerVNC\\vncviewer\\clients", 0,
                           KEY_READ, &hKey);
  if (res != ERROR_SUCCESS) {
    return;
  }

  DWORD count = 0;
  DWORD type = REG_DWORD;
  DWORD size = sizeof(count);
  if (RegQueryValueExW(hKey, L"Count", nullptr, &type, (LPBYTE)&count, &size) == ERROR_SUCCESS) {
    for (DWORD i = 0; i < count; ++i) {
      char keyIP[64], keyUser[64];
      snprintf(keyIP, sizeof(keyIP), "IP_%lu", i);
      snprintf(keyUser, sizeof(keyUser), "User_%lu", i);

      wchar_t wKeyIP[64], wKeyUser[64];
      fl_utf8towc(keyIP, strlen(keyIP)+1, wKeyIP, 64);
      fl_utf8towc(keyUser, strlen(keyUser)+1, wKeyUser, 64);

      wchar_t valIPW[256] = {0};
      wchar_t valUserW[256] = {0};
      DWORD valSize = sizeof(valIPW);
      if (RegQueryValueExW(hKey, wKeyIP, nullptr, nullptr, (LPBYTE)valIPW, &valSize) == ERROR_SUCCESS) {
        char valIP[256] = {0};
        char valUser[256] = {0};
        fl_utf8fromwc(valIP, sizeof(valIP), valIPW, wcslen(valIPW)+1);
        valSize = sizeof(valUserW);
        if (RegQueryValueExW(hKey, wKeyUser, nullptr, nullptr, (LPBYTE)valUserW, &valSize) == ERROR_SUCCESS) {
          fl_utf8fromwc(valUser, sizeof(valUser), valUserW, wcslen(valUserW)+1);
        }
        if (valIP[0] != '\0') {
          ClientEntry ce;
          ce.ip = valIP;
          ce.username = valUser;
          clients.push_back(ce);
        }
      }
    }
  }
  RegCloseKey(hKey);
}

void ServerDialog::saveClients()
{
  HKEY hKey;
  LONG res = RegCreateKeyExW(HKEY_CURRENT_USER,
                             L"Software\\TigerVNC\\vncviewer\\clients", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                             &hKey, nullptr);
  if (res != ERROR_SUCCESS) {
    vlog.error(_("Failed to open registry key for clients: %ld"), res);
    return;
  }

  // Clear existing entries
  DWORD oldCount = 0;
  DWORD type = REG_DWORD;
  DWORD size = sizeof(oldCount);
  if (RegQueryValueExW(hKey, L"Count", nullptr, &type, (LPBYTE)&oldCount, &size) == ERROR_SUCCESS) {
    for (DWORD i = 0; i < oldCount; ++i) {
      char keyIP[64], keyUser[64];
      snprintf(keyIP, sizeof(keyIP), "IP_%lu", i);
      snprintf(keyUser, sizeof(keyUser), "User_%lu", i);
      wchar_t wKeyIP[64], wKeyUser[64];
      fl_utf8towc(keyIP, strlen(keyIP)+1, wKeyIP, 64);
      fl_utf8towc(keyUser, strlen(keyUser)+1, wKeyUser, 64);
      RegDeleteValueW(hKey, wKeyIP);
      RegDeleteValueW(hKey, wKeyUser);
    }
  }

  DWORD count = (DWORD)clients.size();
  RegSetValueExW(hKey, L"Count", 0, REG_DWORD, (const BYTE*)&count, sizeof(count));

  for (DWORD i = 0; i < count; ++i) {
    char keyIP[64], keyUser[64];
    snprintf(keyIP, sizeof(keyIP), "IP_%lu", i);
    snprintf(keyUser, sizeof(keyUser), "User_%lu", i);

    wchar_t wKeyIP[64], wKeyUser[64];
    fl_utf8towc(keyIP, strlen(keyIP)+1, wKeyIP, 64);
    fl_utf8towc(keyUser, strlen(keyUser)+1, wKeyUser, 64);

    wchar_t valIPW[256], valUserW[256];
    fl_utf8towc(clients[i].ip.c_str(), clients[i].ip.size()+1, valIPW, 256);
    fl_utf8towc(clients[i].username.c_str(), clients[i].username.size()+1, valUserW, 256);

    RegSetValueExW(hKey, wKeyIP, 0, REG_SZ, (const BYTE*)valIPW, (wcslen(valIPW)+1)*sizeof(wchar_t));
    RegSetValueExW(hKey, wKeyUser, 0, REG_SZ, (const BYTE*)valUserW, (wcslen(valUserW)+1)*sizeof(wchar_t));
  }

  RegCloseKey(hKey);
}
#else
const char* CLIENTS_FILE="tigervnc.clients";

void ServerDialog::loadClients()
{
  clients.clear();
  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    return;

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, CLIENTS_FILE);
  FILE* f = fopen(filepath, "r");
  if (!f)
    return;

  char line[512];
  while (fgets(line, sizeof(line), f)) {
    char* p = strchr(line, '\r');
    if (p) *p = '\0';
    p = strchr(line, '\n');
    if (p) *p = '\0';

    if (line[0] == '\0')
      continue;

    char* tab = strchr(line, '\t');
    ClientEntry ce;
    if (tab) {
      *tab = '\0';
      ce.ip = line;
      ce.username = tab + 1;
    } else {
      ce.ip = line;
    }
    clients.push_back(ce);
  }
  fclose(f);
}

void ServerDialog::saveClients()
{
  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    return;

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, CLIENTS_FILE);
  FILE* f = fopen(filepath, "w");
  if (!f)
    return;

  for (const auto& client : clients) {
    fprintf(f, "%s\t%s\n", client.ip.c_str(), client.username.c_str());
  }
  fclose(f);
}
#endif

void ServerDialog::loadServerHistory()
{
  std::list<std::string> rawHistory;

  serverHistory.clear();

#ifdef _WIN32
  rawHistory = loadHistoryFromRegKey();
#else

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY);

  /* Read server history from file */
  FILE* f = fopen(filepath, "r");
  if (!f) {
    if (errno == ENOENT) {
      // no history file
      return;
    }
    throw core::posix_error(
      core::format(_("Failed to open \"%s\""), filepath), errno);
  }

  int lineNr = 0;
  while (!feof(f)) {
    char line[256];

    // Read the next line
    lineNr++;
    if (!fgets(line, sizeof(line), f)) {
      if (feof(f))
        break;

      fclose(f);
      throw core::posix_error(
        core::format(_("Failed to read line %d in file \"%s\""),
                     lineNr, filepath),
        errno);
    }

    int len = strlen(line);

    if (len == (sizeof(line) - 1)) {
      fclose(f);
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      throw std::runtime_error(
        core::format("%s: %s", msg.c_str(), _("Line too long")));
    }

    if ((len > 0) && (line[len-1] == '\n')) {
      line[len-1] = '\0';
      len--;
    }
    if ((len > 0) && (line[len-1] == '\r')) {
      line[len-1] = '\0';
      len--;
    }

    if (len == 0)
      continue;

    rawHistory.push_back(line);
  }

  fclose(f);
#endif

  // Filter out duplicates, even if they have different formats
  for (const std::string& entry : rawHistory) {
    if (std::find_if(serverHistory.begin(), serverHistory.end(),
                     [&entry](const std::string& s) {
                       return same_server(s, entry);
                     }) != serverHistory.end())
      continue;
    serverHistory.push_back(entry);
  }
}

void ServerDialog::saveServerHistory()
{
#ifdef _WIN32
  saveHistoryToRegKey(serverHistory);
  return;
#endif

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY);

  /* Write server history to file */
  FILE* f = fopen(filepath, "w+");
  if (!f) {
    std::string msg = core::format(_("Failed to open \"%s\""), filepath);
    throw core::posix_error(msg.c_str(), errno);
  }

  // Save the last X elements to the config file.
  size_t count = 0;
  for (const std::string& entry : serverHistory) {
    if (++count > SERVER_HISTORY_SIZE)
      break;
    fprintf(f, "%s\n", entry.c_str());
  }

  fclose(f);
}

void ServerDialog::updateUsedDir(const char* filename)
{
  char * name = strdup(filename);
  usedDir = dirname(name);
  free(name);
}

void ServerDialog::onServerHistoryRemove(Fl_Widget*, std::string s, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->serverHistory.remove(s);
  dialog->saveServerHistory();
}

std::string ServerDialog::serverHistoryNormalize(const std::string s)
{
  // Convert to lowercase for case-insensitity
  std::string result = s;
  transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
