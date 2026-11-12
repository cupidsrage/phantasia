import java.awt.*;

public class headPne extends Canvas implements constants {

    String title1;
    String title2;
    int cx = 0;
    int canvasWidth = 0;
    int canvasHeight = 0;
    Rectangle theBounds = null;
    FontMetrics fm = null;
    Font theFont = new Font("TimesRoman", Font.BOLD, 14);
    Font theFont2 = new Font("Helvetica", Font.PLAIN, 12);

    public headPne(String message) {

	super();
	title1 = message;
	title2 = "";
	setBackground(highlightColor);
	setSize(38, 38);
    }

    public void paint(Graphics g) {

	if (g == null) {
	    return;
	}

	Graphics2D g2 = Theme.smooth(g);

	theBounds = getBounds();
	canvasWidth = theBounds.width - 1;
	canvasHeight = theBounds.height - 1;

	g2.setColor(highlightColor);
	g2.fillRect(0, 0, canvasWidth + 1, canvasHeight + 1);

	g2.setFont(TitleFont);
	fm = g2.getFontMetrics();
	cx = (canvasWidth - fm.stringWidth(title1)) / 2;
	g2.setColor(accentColor);
	g2.drawString(title1, Math.max(4, cx), fm.getAscent() + 4);

	g2.setFont(LabelFont);
	fm = g2.getFontMetrics();
	cx = (canvasWidth - fm.stringWidth(title2)) / 2;
	g2.setColor(mutedColor);
	g2.drawString(title2, Math.max(4, cx), canvasHeight - 6);
    }

    public Dimension getPreferredSize() {
	return new Dimension(300, 50);
    }

    public void changeHead(String message) {
	title1 = message;
	repaint();
    }

    public void changeTail(String message) {
	title2 = message;
	repaint();
    }
}
