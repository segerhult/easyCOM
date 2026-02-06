#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define MAX_CLIENTS 100

typedef struct {
    int sock;
    char name[64];
    int id;
    int active;
    int bridge_sock;
    int bridge_port;
    GtkTreeRowReference *row_ref;
} ClientInfo;

ClientInfo clients[MAX_CLIENTS];
pthread_mutex_t cs = PTHREAD_MUTEX_INITIALIZER;

GtkListStore *list_store;
GtkWidget *window;
GtkWidget *tree_view;
GtkWidget *map_button;

// Signal for UI updates
enum {
    UPDATE_LIST_SIGNAL,
    LAST_SIGNAL
};

// --- Bridge Thread ---
void *bridge_to_device_thread(void *arg) {
    int id = (int)(intptr_t)arg;
    char buffer[4096];
    int bytesReceived;

    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(clients[id].bridge_port);
    
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        printf("[Error] Could not bind bridge port %d\n", clients[id].bridge_port);
        close(listen_sock);
        return NULL;
    }
    listen(listen_sock, 1);
    
    printf("[Bridge] Listening on port %d for Device %d\n", clients[id].bridge_port, id);

    while (clients[id].active) {
        int app_sock = accept(listen_sock, NULL, NULL);
        if (app_sock < 0) continue;
        
        clients[id].bridge_sock = app_sock;

        // Update UI Status to "Connected"
        g_idle_add((GSourceFunc)gtk_widget_queue_draw, tree_view); // Simple redraw trigger

        while (clients[id].active) {
            bytesReceived = recv(app_sock, buffer, sizeof(buffer), 0);
            if (bytesReceived <= 0) break;
            send(clients[id].sock, buffer, bytesReceived, 0);
        }
        
        close(app_sock);
        clients[id].bridge_sock = -1;
    }
    
    close(listen_sock);
    return NULL;
}

// --- UI Helper to Update List ---
gboolean update_list_ui(gpointer data) {
    (void)data;
    gtk_list_store_clear(list_store);
    
    pthread_mutex_lock(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].active) {
            GtkTreeIter iter;
            gtk_list_store_append(list_store, &iter);
            
            char status[32];
            if (clients[i].bridge_port > 0) {
                snprintf(status, sizeof(status), "Mapped: %d", clients[i].bridge_port);
            } else {
                strcpy(status, "Idle");
            }

            gtk_list_store_set(list_store, &iter, 
                0, clients[i].id,
                1, clients[i].name,
                2, status,
                -1);
        }
    }
    pthread_mutex_unlock(&cs);
    return FALSE; // Run once
}

// --- Client Handler ---
void *client_handler(void *arg) {
    int id = (int)(intptr_t)arg;
    char buffer[4096];
    int bytesReceived;

    // Handshake
    bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = 0;
        if (strncmp(buffer, "REGISTER:", 9) == 0) {
            strncpy(clients[id].name, buffer + 9, 63);
            char *p = strchr(clients[id].name, '\n');
            if (p) *p = 0;
            
            g_idle_add(update_list_ui, NULL);
        }
    }

    while (clients[id].active) {
        bytesReceived = recv(clients[id].sock, buffer, sizeof(buffer), 0);
        if (bytesReceived <= 0) break;

        if (clients[id].bridge_sock != -1) {
            send(clients[id].bridge_sock, buffer, bytesReceived, 0);
        }
    }

    pthread_mutex_lock(&cs);
    clients[id].active = 0;
    close(clients[id].sock);
    pthread_mutex_unlock(&cs);
    
    g_idle_add(update_list_ui, NULL);
    return NULL;
}

// --- Accept Thread ---
void *accept_thread(void *arg) {
    int listenSock = (int)(intptr_t)arg;
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    while (1) {
        int clientSock = accept(listenSock, (struct sockaddr*)&clientAddr, &addrLen);
        if (clientSock < 0) continue;

        pthread_mutex_lock(&cs);
        int id = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].active) {
                id = i;
                break;
            }
        }

        if (id != -1) {
            clients[id].sock = clientSock;
            clients[id].active = 1;
            clients[id].id = id;
            clients[id].bridge_sock = -1;
            clients[id].bridge_port = 0;
            strcpy(clients[id].name, "Connecting...");
            
            pthread_t tid;
            pthread_create(&tid, NULL, client_handler, (void*)(intptr_t)id);
            pthread_detach(tid);
        } else {
            close(clientSock);
        }
        pthread_mutex_unlock(&cs);
        g_idle_add(update_list_ui, NULL);
    }
    return NULL;
}

// --- Button Callback ---
void on_map_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        int id;
        gtk_tree_model_get(model, &iter, 0, &id, -1);

        // Simple Input Dialog
        GtkWidget *dialog = gtk_dialog_new_with_buttons("Map Port",
            GTK_WINDOW(window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Cancel", GTK_RESPONSE_CANCEL,
            "OK", GTK_RESPONSE_ACCEPT,
            NULL);

        GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *entry = gtk_entry_new();
        char default_port[16];
        sprintf(default_port, "%d", 10000 + id);
        gtk_entry_set_text(GTK_ENTRY(entry), default_port);
        gtk_container_add(GTK_CONTAINER(content_area), gtk_label_new("Enter Local TCP Port:"));
        gtk_container_add(GTK_CONTAINER(content_area), entry);
        gtk_widget_show_all(dialog);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
            int port = atoi(text);
            
            pthread_mutex_lock(&cs);
            if (clients[id].active) {
                clients[id].bridge_port = port;
                pthread_t tid;
                pthread_create(&tid, NULL, bridge_to_device_thread, (void*)(intptr_t)id);
                pthread_detach(tid);
            }
            pthread_mutex_unlock(&cs);
            g_idle_add(update_list_ui, NULL);
        }
        gtk_widget_destroy(dialog);
    }
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    // --- Networking Setup ---
    int port = 9000;
    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    int opt = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listenSock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        printf("Bind failed\n");
        return 1;
    }
    listen(listenSock, 5);

    pthread_t tid;
    pthread_create(&tid, NULL, accept_thread, (void*)(intptr_t)listenSock);
    pthread_detach(tid);

    // --- GTK UI Setup ---
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Hub Server (Port 9000)");
    gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    // List View
    list_store = gtk_list_store_new(3, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list_store));
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree_view), -1, "ID", renderer, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree_view), -1, "Name", renderer, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree_view), -1, "Status", renderer, "text", 2, NULL);

    gtk_box_pack_start(GTK_BOX(vbox), tree_view, TRUE, TRUE, 0);

    // Button
    map_button = gtk_button_new_with_label("Map Selected to Port...");
    g_signal_connect(map_button, "clicked", G_CALLBACK(on_map_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), map_button, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();

    return 0;
}
