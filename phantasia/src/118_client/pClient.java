import java.awt.*;
import java.net.*;
import java.io.*;
import java.awt.event.*;
import java.lang.Thread;
import java.awt.event.KeyEvent;

/*
 * pClient - the Phantasia client.
 *
 * Originally a Java applet; browsers and current JDKs no longer support
 * applets, so it now runs as a standalone desktop program:
 *
 *     java -jar phantasia-client.jar [host[:port]] [port]
 *
 * Started with no address (e.g. by double-clicking the jar) it uses the
 * address in /server.txt inside the jar, which the game's website adds to
 * the downloads; failing that it asks for one and remembers it.
 *
 * The host/port can also come from the PHANTASIA_HOST / PHANTASIA_PORT
 * environment variables.  The default port is 43302.
 */
public class pClient extends Panel implements KeyListener, constants {

    static final int DEFAULT_PORT = 43302;

	/* server to connect to */
    String host = "localhost";
    int port = DEFAULT_PORT;

    public static void main(String[] args) {

	String host = System.getenv("PHANTASIA_HOST");
	String portString = System.getenv("PHANTASIA_PORT");

	if (args.length > 0) {
	    host = args[0];
	}

	if (args.length > 1) {
	    portString = args[1];
	}

	    /* use the address the website built into this download */
	if (host == null || host.length() == 0) {
	    host = builtInAddress();
	}

	    /* started without an address (e.g. double-clicked): ask for one */
	if (host == null || host.length() == 0) {
	    host = askForAddress();
	    if (host == null) {
		System.exit(0);
	    }
	}

	    /* accept host:port, which is how Railway shows TCP proxies */
	int colon = host.lastIndexOf(':');
	if (colon > 0 && host.indexOf(':') == colon) {
	    if (portString == null || args.length < 2) {
		portString = host.substring(colon + 1);
	    }
	    host = host.substring(0, colon);
	}

	pClient client = new pClient();
	client.host = host;

	if (portString != null && portString.length() > 0) {
	    try {
		client.port = Integer.parseInt(portString.trim());
	    }
	    catch (NumberFormatException e) {
		System.out.println("Invalid port \"" + portString + "\", using " +
			DEFAULT_PORT);
	    }
	}

	System.out.println("Connecting to " + client.host + ":" + client.port);
	client.init();
    }

	/* the server address stored in the jar as /server.txt, if any */
    static String builtInAddress() {

	InputStream in = pClient.class.getResourceAsStream("/server.txt");

	if (in == null) {
	    return null;
	}

	try {
	    BufferedReader reader = new BufferedReader(new InputStreamReader(in));
	    String line = reader.readLine();
	    reader.close();

	    if (line != null && line.trim().length() > 0) {
		return line.trim();
	    }
	}
	catch (IOException e) {
	    System.out.println("Error reading server.txt: " + e);
	}

	return null;
    }

	/* ask the player for the server address, remembering the last one */
    static String askForAddress() {

	java.util.prefs.Preferences prefs =
		java.util.prefs.Preferences.userNodeForPackage(pClient.class);
	String last = prefs.get("address", "");

	String address = (String) javax.swing.JOptionPane.showInputDialog(null,
		"Enter the game server address (host:port), as shown\n" +
		"on the Phantasia website's \"The Game\" page:",
		"Phantasia v4", javax.swing.JOptionPane.QUESTION_MESSAGE,
		null, null, last);

	if (address == null) {
	    return null;
	}

	address = address.trim();
	if (address.length() == 0) {
	    return null;
	}

	prefs.put("address", address);
	return address;
    }

	/* replacement for the applet parameters sent in the handshake */
    public String getParameter(String name) {

	if (name.equals("time")) {
	    return Long.toString(System.currentTimeMillis() / 1000);
	}

	    /* "num" and "hash": no machine cookie */
	return "0";
    }

	/* load an image from the jar (images/<name>) or the images directory */
    public Image loadImage(String name) {

	URL url = pClient.class.getResource("/images/" + name);

	if (url != null) {
	    return Toolkit.getDefaultToolkit().getImage(url);
	}

	return Toolkit.getDefaultToolkit().getImage("images" + File.separator +
		name);
    }


	/* Main Thread */
    Thread mainThread = null;

	/* Window components */
    Frame f = new Frame("Phantasia v4");
    statusPne status = new statusPne(this);
    buttonPne buttons = new buttonPne(this);
    msgPne messages = new msgPne(this);
    userPne users = new userPne(this);
    chatPne chat = new chatPne(this);
    compassPne compass = new compassPne(this);
    Panel rightPane = new Panel();
    Image theImages[] = new Image[27];

    errorDlog errorDialog = new errorDlog(this);

    Socket socket = null;
    lThread listen = null;
    DataOutputStream output = null;
    BufferedReader input = null;

    int ioStatus = NO_REQUEST;

    public void init()
    {
	mainThread = Thread.currentThread();

	f.addKeyListener(this);

	Theme.install();

	f.setSize(1000, 700);
	f.setMinimumSize(new Dimension(760, 520));
	f.setLocationRelativeTo(null);
	f.setLayout(new GridBagLayout());

	    /* handle the closing of the window */
        f.addWindowListener(new WindowAdapter() {
	    public void windowClosing(WindowEvent evt) {
	        pClientQuit();
	    }
        });
 
	GridBagConstraints constraints = new GridBagConstraints();

	addComponent(status, 0, 0, 2, 1, 0, 0);
	addComponent(messages, 0, 1, 1, 1, 1, 3);
	addComponent(buttons, 0, 2, 1, 1, 0, 0);
	addComponent(chat, 0, 3, 1, 1, 1, 1);
	addComponent(rightPane, 1, 1, 1, 3, 0, 0);

	rightPane.setLayout(new BorderLayout(0, 1));
	rightPane.add("South", compass);
	rightPane.add("Center", users);

	f.setBackground(backgroundColor);
	Theme.apply(f);

/*
	f.pack();
*/
		
	    /* show the frame */
	f.setVisible(true);

	status.loadImages();
	buttons.setImages();

	    /* set up the socket */
        try {
	    socket = new Socket(host, port);
	    output = new DataOutputStream(socket.getOutputStream());
	    input = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        }
        catch (Exception e) {
            System.out.println("Error: " + e);
	    errorDialog.bringUp("The system can not connect to the server.",
		    "The server could be down or a firewall could exist between you and it.",
		    "Please try again later.");
	    return;
        }

	    /* start the listen thread */
	listen = new lThread(this);
	listen.start();
    }

    private void addComponent(Panel item, int x, int y, int width, int height, int weightx, int weighty) {

	GridBagConstraints constraints = new GridBagConstraints();

	    /* add the status area to the frame */
	constraints.gridx = x;
	constraints.gridy = y;
	constraints.gridwidth = width;
	constraints.gridheight = height;
	constraints.weightx = weightx;
	constraints.weighty = weighty;
	constraints.fill = GridBagConstraints.BOTH;
	constraints.anchor = GridBagConstraints.CENTER;
	constraints.insets.top = 2;
	constraints.insets.bottom = 2;
	constraints.insets.left = 2;
	constraints.insets.right = 2;
	((GridBagLayout)f.getLayout()).setConstraints(item, constraints);
	f.add(item);
    }

    public void pClientQuit() {

System.out.println("pClientQuit called.");

	    /* destroy the listen thread */
	if (listen != null) {
	    listen.running = false;
	    listen = null;
	}

System.out.println("Listen Thread Stopped.");
	    /* close the socket */
	if (socket != null) {
	    try {
	        socket.close();
	    }
	    catch (IOException e) {
	        System.out.println("Error: " + e);
	    }
	    socket = null;
	}
System.out.println("Socket Closed.");

	    /* remove the main windw */
	f.setVisible(false);
System.out.println("Main window hidden.");
	f.dispose();
System.out.println("Main window disposed.");
	System.exit(0);
    }

    public String readString() {

	String message = "";
	
	    try {
		message = input.readLine();
	    }
	    catch (Exception e) {
		System.out.println("Error: " + e);
	        errorDialog.bringUp("There was an error reading from the socket.",
		        "readString error: " + e, "The game will now terminate.");
	    }
/*
System.out.println("Returning from socket: " + message);
*/
	return(message);
    }

    public long readLong() {

	String message = readString();

	    /* the server closed the connection */
	if (message == null) {
	    System.out.println("Connection closed by the server.");
	    pClientQuit();
	}

	return(Long.parseLong(message.trim()));
    }


    public int readBool() {

	String message = "";
	
	try {
	    message = input.readLine();
	}
	catch (Exception e) {
	    System.out.println("Error: " + e);
	    errorDialog.bringUp("There was an error reading from the socket.",
	            "readString error: " + e, "The game will now terminate.");
	}

	if (message.equals("No")) {
	    return 0;
	}
	else if (message.equals("Yes")) {
	    return 1;
	}
	else {
	    System.out.println("Error: readBool read the string " + message);
	    errorDialog.bringUp("There was an error reading from the socket.",
	            "readBool read the string " + message,
		    "The game will now terminate.");
   	} 
	return 0;
}

    synchronized public void sendString(String theString) {
	try {
	    output.writeBytes(theString);
	}
	catch (Exception e) {
	        System.out.println("Error: " + e);
	        errorDialog.bringUp("There was an error writing to the socket.",
		        "sendString error: " + e, "The game will now terminate.");
	}
    }

    synchronized public boolean pollSendFlag(int ioArea) {
/*
System.out.println("Request Send flag - Poll=" + Integer.toString(ioArea) + "  Status=" + Integer.toString(ioStatus));
*/
	if (ioStatus == ioArea) {
	    ioStatus = NO_REQUEST;
	    return true;
	}

	return false;
    }

    public void raiseSendFlag(int ioArea) {

	if (ioStatus != NO_REQUEST) {
	    errorDialog.bringUp("Client attempted to have two i/o sources.",
		    "The game will now terminate.", "");
	}

	ioStatus = ioArea;
    }

    public void handlePing() {

	int ioArea = ioStatus;

	if (ioStatus != NO_REQUEST && pollSendFlag(ioArea)) {

	    sendString(C_PING_PACKET);

	    switch(ioArea) {

	    case BUTTONS:
	        buttons.timeout();
		break;

	    case STRING_DLOG:
		listen.stringDialog.timeout();
		break;

	    case COORD_DLOG:
		listen.coordinateDialog.timeout();
		break;

	    case PLAYER_DLOG:
		listen.playerDialog.timeout();
		break;
	    }
	}
    }

    public void keyPressed(KeyEvent evt) {

	int theKey;

	theKey = evt.getKeyCode();

	switch(theKey) {

	case KeyEvent.VK_1:
	case KeyEvent.VK_F1:
	    buttons.DoButton(0);
	    break;
	case KeyEvent.VK_2:
	case KeyEvent.VK_F2:
	    buttons.DoButton(1);
	    break;
	case KeyEvent.VK_3:
	case KeyEvent.VK_F3:
	    buttons.DoButton(2);
	    break;
	case KeyEvent.VK_4:
	case KeyEvent.VK_F4:
	    buttons.DoButton(3);
	    break;
	case KeyEvent.VK_5:
	case KeyEvent.VK_F5:
	    buttons.DoButton(4);
	    break;
	case KeyEvent.VK_6:
	case KeyEvent.VK_F6:
	    buttons.DoButton(5);
	    break;
	case KeyEvent.VK_7:
	case KeyEvent.VK_F7:
	    buttons.DoButton(6);
	    break;
	case KeyEvent.VK_8:
	case KeyEvent.VK_F8:
	    buttons.DoButton(7);
	    break;
	case KeyEvent.VK_NUMPAD1:
	    compass.DoButton(6);
	    break;
	case KeyEvent.VK_NUMPAD2:
	    compass.DoButton(7);
	    break;
	case KeyEvent.VK_NUMPAD3:
	    compass.DoButton(8);
	    break;
	case KeyEvent.VK_NUMPAD4:
	    compass.DoButton(3);
	    break;
	case KeyEvent.VK_NUMPAD5:
	    compass.DoButton(4);
	    break;
	case KeyEvent.VK_NUMPAD6:
	    compass.DoButton(5);
	    break;
	case KeyEvent.VK_NUMPAD7:
	    compass.DoButton(0);
	    break;
	case KeyEvent.VK_NUMPAD8:
	    compass.DoButton(1);
	    break;
	case KeyEvent.VK_NUMPAD9:
	    compass.DoButton(2);
	    break;
	case KeyEvent.VK_SPACE:
	    buttons.spacebar();
	    break;
	}

	return;
    }

    public void keyReleased(KeyEvent evt) {;}
    public void keyTyped(KeyEvent evt) {;}
}
