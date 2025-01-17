#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "Includes/VisionProtos/ssl_vision_detection.pb.h"
#include "Includes/VisionProtos/ssl_vision_geometry.pb.h"
#include "Includes/VisionProtos/ssl_vision_wrapper.pb.h"
#include "Includes/GRSimProtos/grSim_Packet.pb.h"
#include "Includes/VisionProtos/timer.h"
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), udpsocket_rec(this)
{
    ui->setupUi(this);

    QStringList teams, modes;
    teams << "Blue" << "Yellow";
    modes << "Simulation" << "Reality";
    ui->team->addItems(teams);
    ui->mode->addItems(modes);

    timer = new QTimer(this);
    timer->setInterval(20);

    // Conectar o slot de leitura para processar os dados recebidos
    connect(&udpsocket_rec, &QUdpSocket::readyRead, this, &MainWindow::processPendingDatagrams);
    connect(ui->Receive, SIGNAL(clicked()), this, SLOT(receiveBtnClicked()));
    connect(ui->StopReceiving, SIGNAL(clicked()), this, SLOT(stopReceiving()));
    connect(ui->Connect, SIGNAL(clicked()), this, SLOT(reconnectUdp()));
    connect(ui->Send, SIGNAL(clicked()), this, SLOT(sendBtnClicked()));
    // connect(timer, SIGNAL(timeout()), this, SLOT(sendPacket()));
    ui->Send->setDisabled(true);
    sending = false;
}

MainWindow::~MainWindow()
{
    delete ui;
}

/*
Acho que teria que fazer uma gambiarra de passar os dois message, do grSim e da vida real, e
uma variavel booleana pra decidir qual dos dois usar.
Ou deixar os dois com o mesmo nome, meio que fazer uma classe abstrata.
*/
void MainWindow::strategyAndSend(int i,SSL_DetectionRobot robot, grSim_Packet packet)
{

    grSim_Robot_Command *command = packet.mutable_commands()->add_robot_commands();

    command->set_id(i);
    command->set_veltangent(robot.x() >= 0 ? 0.5 : -0.5);
    command->set_wheelsspeed(!true);
    command->set_wheel1(0);
    command->set_wheel2(0);
    command->set_wheel3(0);
    command->set_wheel4(0);
    command->set_velnormal(0);
    command->set_velangular(0);
    command->set_kickspeedx(0);
    command->set_kickspeedz(0);
    command->set_spinner(false);

    QByteArray dgram;
    dgram.resize(packet.ByteSizeLong());
    if (packet.SerializeToArray(dgram.data(), dgram.size()))
    {
        qDebug() << "Pacote serializado com sucesso. Tamanho do datagrama:" << dgram.size();
        udpsocket.writeDatagram(dgram, _addr, _port);
    }
    else
    {
        qDebug() << "Falha na serialização do pacote.";
    }
}




/*a ideia é fazer essa funçao servir pro simulado e pro real.
 mas pra isso precisa que no real a gente implemente a mesma interface
do .proto do grSim. ai passava os message como parametros.
*/
void MainWindow::simulationStrategy(SSL_DetectionFrame detection)
{
    qDebug() << "Estratégia Simulada"; // Debug

    grSim_Packet packet;
    bool yellow = ui->team->currentText() == "Yellow";

    packet.mutable_commands()->set_isteamyellow(yellow);
    packet.mutable_commands()->set_timestamp(0.0);

    int balls_n = detection.balls_size();
    int robots_blue_n = detection.robots_blue_size();
    int robots_yellow_n = detection.robots_yellow_size();

    if (yellow)
    {
        // Yellow robot info:
        for (int i = 0; i < robots_yellow_n; i++)
        {

            SSL_DetectionRobot robot = detection.robots_yellow(i);
            strategyAndSend(i, robot, packet);

        }
    }
    else
    {
        // Blue robot info:
        for (int i = 0; i < robots_blue_n; i++)
        {
            SSL_DetectionRobot robot = detection.robots_blue(i);
            strategyAndSend(i, robot, packet);
        }
    }
}

void MainWindow::realStrategy(SSL_DetectionFrame detection)
{
    qDebug() << "Estratégia Real"; // Debug
}

void MainWindow::sendPacket(SSL_DetectionFrame detection)
{
    qDebug() << "Iniciando o envio do pacote"; // Debug
    qDebug() << ui->mode->currentText();       // Debug
    if (ui->mode->currentText() == "Simulation")
    {
        // Lógica para a simulação
        simulationStrategy(detection);
    }
    else
    {
        // Lógica para os robôs reais
        realStrategy(detection);
    }
}

void MainWindow::sendBtnClicked()
{
    sending = !sending;
    if (!sending)
    {
        // timer->stop();
        ui->Send->setText("Send");
    }
    else
    {
        // timer->start();
        ui->Send->setText("Pause");
    }
}

void MainWindow::reconnectUdp()
{
    // Obtenha o IP e a porta do QLineEdit
    _addr = QHostAddress(ui->_addr->text()); // IP do QLineEdit
    _port = ui->_port->text().toUShort();    // Porta do QLineEdit

    qDebug() << "Endereço IP:" << _addr.toString();
    qDebug() << "Endereço IP:" << _addr.toString() << ", Porta:" << _port;

    // Habilite o botão de envio
    ui->Send->setDisabled(false);
}

void MainWindow::receiveBtnClicked()
{
    qDebug() << "Botão 'Receive' clicado. Configurando socket...";
    // Configurar o endereço e porta de multicast
    QHostAddress multicastAddress("224.5.23.2");
    quint16 port = 10020;
    //  Fechar o socket atual se estiver aberto
    if (udpsocket_rec.isOpen())
    {
        udpsocket_rec.close();
    }

    // Configurar o socket para escutar no endereço multicast
    udpsocket_rec.bind(QHostAddress::AnyIPv4, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    udpsocket_rec.joinMulticastGroup(multicastAddress);

    connect(&udpsocket_rec, &QUdpSocket::readyRead, this, &MainWindow::processPendingDatagrams);
}

void MainWindow::processPendingDatagrams()
{
    while (udpsocket_rec.hasPendingDatagrams())
    {
        QByteArray datagram;
        datagram.resize(udpsocket_rec.pendingDatagramSize());
        udpsocket_rec.readDatagram(datagram.data(), datagram.size());

        // Variável para acumular as informações para o txtInfo
        QString info;

        // Criar um pacote SSL_WrapperPacket e tentar analisar os dados recebidos
        SSL_WrapperPacket packet;
        if (packet.ParseFromArray(datagram.data(), datagram.size()))
        {
            if (packet.has_detection())
            {
                SSL_DetectionFrame detection = packet.detection();
                double t_now = GetTimeSec();

                if (sending)
                {
                    sendPacket(detection);
                }

                // Adiciona a informação de detecção no terminal e no txtInfo
                const char *header = "-[Detection Data]-------\n";
                printf("%s", header);
                info += "-[Detection Data]-------\n";

                // Frame info
                QString frameInfo = QString("Camera ID=%1 FRAME=%2 T_CAPTURE=%.4f ")
                                        .arg(detection.camera_id())
                                        .arg(detection.frame_number())
                                        .arg(detection.t_capture());
                printf("Camera ID=%d FRAME=%d T_CAPTURE=%.4f ", detection.camera_id(), detection.frame_number(), detection.t_capture());
                info += frameInfo;

                // SSL-Vision Processing Latency
                double sslLatency = (detection.t_sent() - detection.t_capture()) * 1000.0;
                printf("SSL-Vision Processing Latency                   %7.3fms\n", sslLatency);
                info += QString("SSL-Vision Processing Latency                   %1ms\n").arg(sslLatency, 0, 'f', 3);

                // Network Latency
                double networkLatency = (t_now - detection.t_sent()) * 1000.0;
                printf("Network Latency (assuming synched system clock) %7.3fms\n", networkLatency);
                info += QString("Network Latency (assuming synched system clock) %1ms\n").arg(networkLatency, 0, 'f', 3);

                // Total Latency
                double totalLatency = (t_now - detection.t_capture()) * 1000.0;
                printf("Total Latency   (assuming synched system clock) %7.3fms\n", totalLatency);
                info += QString("Total Latency   (assuming synched system clock) %1ms\n").arg(totalLatency, 0, 'f', 3);

                int balls_n = detection.balls_size();
                int robots_blue_n = detection.robots_blue_size();
                int robots_yellow_n = detection.robots_yellow_size();
                info += QString("Robots blue   %1\n").arg(robots_blue_n);
                info += QString("Robots Yellow   %1\n").arg(robots_yellow_n);
                // Ball info:
                for (int i = 0; i < balls_n; i++)
                {
                    SSL_DetectionBall ball = detection.balls(i);
                    QString ballInfo = QString("-Ball (%1/%2): CONF=%3 POS=<%4,%5> ")
                                           .arg(i + 1)
                                           .arg(balls_n)
                                           .arg(ball.confidence(), 0, 'f', 2)
                                           .arg(ball.x(), 0, 'f', 2)
                                           .arg(ball.y(), 0, 'f', 2);
                    printf("-Ball (%2d/%2d): CONF=%4.2f POS=<%9.2f,%9.2f> ", i + 1, balls_n, ball.confidence(), ball.x(), ball.y());
                    info += ballInfo;

                    // Z coordinate
                    if (ball.has_z())
                    {
                        QString zInfo = QString("Z=%1 ").arg(ball.z(), 0, 'f', 2);
                        printf("Z=%7.2f ", ball.z());
                        info += zInfo;
                    }
                    else
                    {
                        printf("Z=N/A   ");
                        info += "Z=N/A   ";
                    }

                    // RAW coordinates
                    QString rawInfo = QString("RAW=<%1,%2>\n")
                                          .arg(ball.pixel_x(), 0, 'f', 2)
                                          .arg(ball.pixel_y(), 0, 'f', 2);
                    printf("RAW=<%8.2f,%8.2f>\n", ball.pixel_x(), ball.pixel_y());
                    info += rawInfo;
                }

                // Blue robot info:
                for (int i = 0; i < robots_blue_n; i++)
                {
                    SSL_DetectionRobot robot = detection.robots_blue(i);
                    QString robotHeader = QString("-Robot(B) (%1/%2): ").arg(i + 1).arg(robots_blue_n);
                    printf("-Robot(B) (%2d/%2d): ", i + 1, robots_blue_n);
                    info += robotHeader;

                    // ID
                    QString idInfo = robot.has_robot_id() ? QString("Robot ID=%1 ").arg(robot.robot_id()) : QString("Robot ID=N/A ");
                    if (robot.has_robot_id())
                        printf("Robot ID=%d ", robot.robot_id());
                    else
                        printf("Robot ID=N/A ");
                    info += idInfo;

                    // CONF
                    QString confInfo = QString("CONF=%1 ").arg(robot.confidence(), 0, 'f', 2);
                    printf("CONF=%4.2f ", robot.confidence());
                    info += confInfo;

                    // POS
                    QString posInfo = QString("POS=<%1,%2> ")
                                          .arg(robot.x(), 0, 'f', 2)
                                          .arg(robot.y(), 0, 'f', 2);
                    printf("POS=<%9.2f,%9.2f> ", robot.x(), robot.y());
                    info += posInfo;

                    // ANGLE
                    if (robot.has_orientation())
                    {
                        QString angleInfo = QString("ANGLE=%1 ").arg(robot.orientation(), 0, 'f', 3);
                        printf("ANGLE=%7.3f ", robot.orientation());
                        info += angleInfo;
                    }
                    else
                    {
                        printf("ANGLE=N/A ");
                        info += "ANGLE=N/A ";
                    }

                    // RAW coordinates
                    QString rawRobotInfo = QString("RAW=<%1,%2>\n")
                                               .arg(robot.pixel_x(), 0, 'f', 2)
                                               .arg(robot.pixel_y(), 0, 'f', 2);
                    printf("RAW=<%8.2f,%8.2f>\n", robot.pixel_x(), robot.pixel_y());
                    info += rawRobotInfo;
                }

                // Yellow robot info:

                for (int i = 0; i < robots_yellow_n; i++)
                {
                    SSL_DetectionRobot robot = detection.robots_yellow(i);
                    QString robotHeader = QString("-Robot(Y) (%1/%2): ").arg(i + 1).arg(robots_yellow_n);
                    printf("-Robot(Y) (%2d/%2d): ", i + 1, robots_yellow_n);
                    info += robotHeader;

                    // ID
                    QString idInfo = robot.has_robot_id() ? QString("Robot ID=%1 ").arg(robot.robot_id()) : QString("Robot ID=N/A ");
                    if (robot.has_robot_id())
                        printf("Robot ID=%d ", robot.robot_id());
                    else
                        printf("Robot ID=N/A ");
                    info += idInfo;

                    // CONF
                    QString confInfo = QString("CONF=%1 ").arg(robot.confidence(), 0, 'f', 2);
                    printf("CONF=%4.2f ", robot.confidence());
                    info += confInfo;

                    // POS
                    QString posInfo = QString("POS=<%1,%2> ")
                                          .arg(robot.x(), 0, 'f', 2)
                                          .arg(robot.y(), 0, 'f', 2);
                    printf("POS=<%9.2f,%9.2f> ", robot.x(), robot.y());
                    info += posInfo;

                    // ANGLE
                    if (robot.has_orientation())
                    {
                        QString angleInfo = QString("ANGLE=%1 ").arg(robot.orientation(), 0, 'f', 3);
                        printf("ANGLE=%7.3f ", robot.orientation());
                        info += angleInfo;
                    }
                    else
                    {
                        printf("ANGLE=N/A ");
                        info += "ANGLE=N/A ";
                    }

                    // RAW coordinates
                    QString rawRobotInfo = QString("RAW=<%1,%2>\n")
                                               .arg(robot.pixel_x(), 0, 'f', 2)
                                               .arg(robot.pixel_y(), 0, 'f', 2);
                    printf("RAW=<%8.2f,%8.2f>\n", robot.pixel_x(), robot.pixel_y());
                    info += rawRobotInfo;
                }

                // Exibir uma linha de separação após cada pacote processado (opcional)
                const char *separator = "-------------------------\n";
                printf("%s", separator);
                info += "-------------------------\n";

                // Atualiza o QTextEdit (txtInfo) com as informações acumuladas
                ui->txtInfo->append(info);
            }
        }
    }
}

void MainWindow::stopReceiving()
{

    udpsocket_rec.close();
    // Fecha o socket para interromper o recebimento
    disconnect(&udpsocket_rec, &QUdpSocket::readyRead, this, &MainWindow::processPendingDatagrams); // Desconecta o sinal
    ui->txtInfo->append("Recebimento de dados interrompido.");                                      // Mensagem de log

}

// Efeitos de cliques nos botões

void MainWindow::on_Receive_pressed()
{
    ui->Receive->setStyleSheet("background-color: #FF5733; border: 2px solid #FFD700;");
}

void MainWindow::on_Receive_released()
{
    ui->Receive->setStyleSheet("background-color: #E01B25; color: #D1D1D1; border: 2px solid black;");
}

void MainWindow::on_StopReceiving_pressed()
{
    ui->StopReceiving->setStyleSheet("background-color: #FF5733; border: 2px solid #FFD700;");
}

void MainWindow::on_StopReceiving_released()
{
    ui->StopReceiving->setStyleSheet("background-color: #E01B25; color: #D1D1D1; border: 2px solid black;");
}

void MainWindow::on_Connect_pressed()
{
    ui->Connect->setStyleSheet("background-color: #FF5733; border: 2px solid #FFD700;");
}

void MainWindow::on_Connect_released()
{
    ui->Connect->setStyleSheet("background-color: #E01B25; color: #D1D1D1; border: 2px solid black;");
}

void MainWindow::on_Send_pressed()
{
    ui->Send->setStyleSheet("background-color: #FF5733; border: 2px solid #FFD700;");
}

void MainWindow::on_Send_released()
{
    ui->Send->setStyleSheet("background-color: #E01B25; color: #D1D1D1; border: 2px solid black;");
}
