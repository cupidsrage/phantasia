import java.awt.*;

public class titlePne extends Canvas implements constants {

    String theValue;
    int cx = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    Rectangle theBounds = null;
    FontMetrics fm = null;
    Font theFont = new Font("Helvetica", Font.BOLD, 12);

    public titlePne(String message) {

	super();
	theValue = message;
	setBackground(highlightColor);
    }

    public void paint(Graphics g) {

	Graphics2D g2 = Theme.smooth(g);

	theBounds = getBounds();
	canvasWidth = theBounds.width - 1;
	canvasHeight = theBounds.height - 1;

	g2.setColor(highlightColor);
	g2.fillRect(0, 0, canvasWidth + 1, canvasHeight + 1);

	g2.setFont(LabelFont);
	fm = g2.getFontMetrics();
	g2.setColor(mutedColor);
	g2.drawString(theValue, 2, canvasHeight - 4);
    }

    public Dimension getPreferredSize() {
	return new Dimension(60, 20);
    }
}
